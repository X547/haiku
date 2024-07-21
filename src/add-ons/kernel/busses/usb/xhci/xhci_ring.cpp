#include "xhci.h"

#include <algorithm>
#include <malloc.h>
#include <string.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


status_t
XhciRingSegment::Init(bool cycleBit)
{
	fTrbs = (xhci_trb*)memalign(B_PAGE_SIZE, kMaxLength*sizeof(xhci_trb));
	if (fTrbs == NULL)
		return B_NO_MEMORY;

	physical_entry pe;
	get_memory_map(fTrbs, B_PAGE_SIZE, &pe, 1);
	fTrbAddr = pe.address;

	for (xhci_trb* trb = fTrbs; trb != fTrbs + kMaxUsableLength; trb++) {
		*trb = {
			.flags = (uint32)cycleBit << TRB_3_CYCLE_BIT
		};
	}
	xhci_trb* linkTrb = fTrbs + kMaxUsableLength;
	*linkTrb = {
		.flags
			= TRB_3_TYPE(TRB_TYPE_LINK)
			| ((uint32)cycleBit << TRB_3_CYCLE_BIT)
	};

	return B_OK;
}


XhciRingSegment::~XhciRingSegment()
{
	free(fTrbs);
}


// #pragma mark - XhciRing

XhciRing::~XhciRing()
{
	XhciTransferDesc* td;
	while ((td = fTransferDescs.RemoveHead()) != NULL)
		delete td;

	// Delete segment chain
	XhciRingSegment* seg = fEnqueue.seg;
	if (seg != NULL) {
		XhciRingSegment* nextSeg;
		do {
			nextSeg = seg->fNext;
			delete seg;
			seg = nextSeg;
		} while (nextSeg != fEnqueue.seg);
	}
}


status_t
XhciRing::Init(uint32 segmentCount)
{
	XhciRingSegment* lastSegment = NULL;

	for (; segmentCount > 0; segmentCount--) {
		ObjectDeleter<XhciRingSegment> newSegment(new(std::nothrow) XhciRingSegment());
		if (!newSegment.IsSet())
			return B_NO_MEMORY;
		CHECK_RET(newSegment->Init(false));

		if (lastSegment == NULL) {
			lastSegment = newSegment.Get();
			lastSegment->SetNext(lastSegment);
			fEnqueue = XhciRingRider(lastSegment);
			fDequeue = XhciRingRider(lastSegment);
		} else {
			newSegment->SetNext(fEnqueue.seg);
			lastSegment->SetNext(newSegment.Get());
			lastSegment = newSegment.Get();
		}
		newSegment.Detach();
	}

	// Set cycle toggle bit for last segment link TRB
	lastSegment->LinkTrb()->flags |= (1U << TRB_3_TC_BIT);

	return B_OK;
}


status_t
XhciRing::Alloc(XhciRingRider& rd, bool chain)
{
	rd.Inc();
	if (!rd.IsLink())
		return B_OK;

	rd.seg->LinkTrb()->flags
		= (rd.seg->LinkTrb()->flags & ~(uint32)((1U << TRB_3_CYCLE_BIT) | (1U << TRB_3_CHAIN_BIT)))
		| ((uint32)rd.cycleBit << TRB_3_CYCLE_BIT)
		| ((uint32)chain << TRB_3_CHAIN_BIT);

	XhciRingRider prevRd = rd;
	rd.Inc();
	if (rd.seg != fDequeue.seg)
		return B_OK;

	TRACE("XhciRing: Allocate new segment\n");
	ObjectDeleter<XhciRingSegment> newSegment(new(std::nothrow) XhciRingSegment());
	if (!newSegment.IsSet())
		return B_NO_MEMORY;
	CHECK_RET(newSegment->Init(!prevRd.cycleBit));

	newSegment->SetNext(rd.seg);
	prevRd.seg->SetNext(newSegment.Get());
	rd = XhciRingRider(newSegment.Get());
	newSegment.Detach();

	return B_OK;
}


void
XhciRing::Commit(const XhciRingRider& newEnqueue)
{
	TRACE("XhciRing::Commit()\n");
	TRACE("  fEnqueue: %#" B_PRIx64 "\n", fEnqueue.PhysAddr());

#if TRACE_USB
	for (XhciRingRider rd = fEnqueue; rd != newEnqueue; rd.Inc()) {
		DumpTrb(*rd.trb);
	}
#endif

	fEnqueue.trb->flags ^= (1U << TRB_3_CYCLE_BIT);
	fEnqueue = newEnqueue;
}


status_t
XhciRing::SubmitTransfer(XHCI& xhci, UsbBusTransfer* transfer)
{
	UsbBusPipe* pipe = transfer->TransferPipe();
	XhciEndpoint* endpoint = (XhciEndpoint*)pipe->ControllerCookie();

	ObjectDeleter<XhciTransferDesc> td(new(std::nothrow) XhciTransferDesc(xhci.fStack));
	if (!td.IsSet())
		return B_NO_MEMORY;

	td->fTransfer = transfer;
	CHECK_RET(td->FillTransfer(xhci, *this));

	fTransferDescs.Insert(td.Get());

	Commit(td->fEnd);
	xhci.Ring(endpoint->fDevice->fSlot, endpoint->fId + 1);
	td.Detach();

	return B_OK;
}


void
XhciRing::CompleteTransfer(XHCI& xhci, MutexLocker& locker, const xhci_trb& eventTrb)
{
	TRACE("XhciRing::CompleteTransfer()\n");
	TRACE("Event TRB:\n");
	//DumpTrb(eventTrb);

	const uint8 completionCode = TRB_2_COMP_CODE_GET(eventTrb.status);
	const uint32 remainder = TRB_2_REM_GET(eventTrb.status);
	const phys_addr_t source = eventTrb.address;

	int32 tdIndex;
	size_t completedLen;
	XhciTransferDesc* td = LookupTransferDescTrb(source, tdIndex, completedLen);

	if (td == NULL) {
		dprintf("TD referenced in completion event not found in the ring\n");
#if 0
		dprintf("Event TRB:\n");
		DumpTrb(eventTrb);

		dprintf("TD List:\n");
		for (XhciTransferDesc* td = fTransferDescs.First(); td != NULL; td = fTransferDescs.GetNext(td)) {
			dprintf("TD:\n");
			for (XhciRingRider rd = td->fBegin; rd != td->fEnd; rd.Inc()) {
				DumpTrb(*rd.trb);
			}
		}
#endif
		return;
	}
	size_t transferedLen = completedLen - remainder;

	TRACE("tdIndex: %" B_PRId32
		", transferedLen: %" B_PRIuSIZE
		", completedLen: %" B_PRIuSIZE
		", remainder: %" B_PRIu32 "\n",
		tdIndex,
		transferedLen,
		completedLen,
		remainder);

	UsbBusPipe* pipe = td->fTransfer->TransferPipe();
	bool directionIn = (pipe->Direction() != UsbBusPipe::Out);
	usb_isochronous_data* isochronousData = td->fTransfer->IsochronousData();

	status_t callbackStatus = B_OK;
	switch (completionCode) {
		case COMP_SHORT_PACKET:
		case COMP_SUCCESS:
			callbackStatus = B_OK;
			break;
		case COMP_DATA_BUFFER:
			callbackStatus = directionIn ? B_DEV_DATA_OVERRUN
				: B_DEV_DATA_UNDERRUN;
			break;
		case COMP_BABBLE:
			callbackStatus = directionIn ? B_DEV_FIFO_OVERRUN
				: B_DEV_FIFO_UNDERRUN;
			break;
		case COMP_USB_TRANSACTION:
			callbackStatus = B_DEV_CRC_ERROR;
			break;
		case COMP_STALL:
			callbackStatus = B_DEV_STALLED;
			break;
		default:
			callbackStatus = B_DEV_STALLED;
			break;
	}

	if (isochronousData != NULL) {
		auto& desc = isochronousData->packet_descriptors[tdIndex];
		desc.actual_length = transferedLen;
		desc.status = callbackStatus;

		if (td->fCompletionStatus >= B_OK && callbackStatus < B_OK)
			td->fCompletionStatus = callbackStatus;

		td->fTransferred += transferedLen;

		if ((uint32)tdIndex != isochronousData->packet_count - 1)
			return;
	} else {
		td->fCompletionStatus = callbackStatus;
		td->fTransferred = transferedLen;
	}

	Complete(td->fEnd);
	fTransferDescs.Remove(td);

	locker.Unlock();
	xhci.fCallbackQueue.Add(&td->fDpcCallback);
}


status_t
XhciRing::CancelAllTransfers(XHCI& xhci, MutexLocker& locker, XhciEndpoint* endpoint)
{
	if (fTransferDescs.IsEmpty())
		return B_OK;

	locker.Unlock();
	status_t status = xhci.StopEndpoint(false, endpoint);
	if (status != B_OK && status != B_DEV_STALLED) {
		// It is possible that the endpoint was stopped by the controller at the
		// same time our STOP command was in progress, causing a "Context State"
		// error. In that case, try again; if the endpoint is already stopped,
		// StopEndpoint will notice this. (XHCI 1.2 § 4.6.9 p137.)
		status = xhci.StopEndpoint(false, endpoint);
	}
	if (status == B_DEV_STALLED) {
		// Only exit from a Halted state is a RESET. (XHCI 1.2 § 4.8.3 p163.)
		TRACE_ERROR("cancel queued transfers: halted endpoint, reset!\n");
		status = xhci.ResetEndpoint(false, endpoint);
	}
	xhci.ProcessEvents();
	locker.Lock();

	XhciTransferDesc* td;
	while ((td = fTransferDescs.RemoveHead()) != NULL) {
		td->fTransfer->Finished(B_CANCELED, 0);
		td->fTransfer->Free();
		delete td;
	}

	fDequeue.trb->flags ^= (1U << TRB_3_CYCLE_BIT);
	fEnqueue = fDequeue;

	phys_addr_t dequeuePhysAddr
		= fEnqueue.PhysAddr()
		| (fDequeue.cycleBit ? ENDPOINT_2_DCS_BIT : 0);

	xhci.SetTRDequeue(dequeuePhysAddr, 0, endpoint->fId + 1, endpoint->fDevice->fSlot);

	return B_OK;
}


XhciTransferDesc*
XhciRing::LookupTransferDesc(phys_addr_t addr)
{
	for (XhciTransferDesc* td = fTransferDescs.First(); td != NULL; td = fTransferDescs.GetNext(td)) {
		if (td->fBegin.PhysAddr() == addr)
			return td;
	}
	return NULL;
}


XhciTransferDesc*
XhciRing::LookupTransferDescTrb(phys_addr_t addr, int32& tdIndex, size_t& completedLen)
{
	tdIndex = -1;
	completedLen = 0;
	for (XhciTransferDesc* td = fTransferDescs.First(); td != NULL; td = fTransferDescs.GetNext(td)) {
		bool prevChainBit = false;
		for (XhciRingRider rd = td->fBegin; rd != td->fEnd; rd.Inc()) {
			switch (TRB_3_TYPE_GET(rd.trb->flags)) {
				case TRB_TYPE_DATA_STAGE:
				case TRB_TYPE_NORMAL:
				case TRB_TYPE_ISOCH:
					if (!prevChainBit) {
						tdIndex++;
						completedLen = 0;
					}
					prevChainBit = (rd.trb->flags & (1U << TRB_3_CHAIN_BIT)) != 0;

					completedLen += TRB_2_BYTES(rd.trb->status);
					break;
			}

			if (rd.PhysAddr() == addr)
				return td;

		}
		tdIndex = -1;
		completedLen = 0;
	}
	return NULL;
}


static const char*
xhci_trb_type_string(uint32 trbType)
{
	const char* typeStr;
	switch (trbType) {
		case TRB_TYPE_NORMAL: typeStr = "NORMAL"; break;
		case TRB_TYPE_SETUP_STAGE: typeStr = "SETUP_STAGE"; break;
		case TRB_TYPE_DATA_STAGE: typeStr = "DATA_STAGE"; break;
		case TRB_TYPE_STATUS_STAGE: typeStr = "STATUS_STAGE"; break;
		case TRB_TYPE_ISOCH: typeStr = "ISOCH"; break;
		case TRB_TYPE_LINK: typeStr = "LINK"; break;
		case TRB_TYPE_EVENT_DATA: typeStr = "EVENT_DATA"; break;
		case TRB_TYPE_TR_NOOP: typeStr = "TR_NOOP"; break;
		case TRB_TYPE_ENABLE_SLOT: typeStr = "ENABLE_SLOT"; break;
		case TRB_TYPE_DISABLE_SLOT: typeStr = "DISABLE_SLOT"; break;
		case TRB_TYPE_ADDRESS_DEVICE: typeStr = "ADDRESS_DEVICE"; break;
		case TRB_TYPE_CONFIGURE_ENDPOINT: typeStr = "CONFIGURE_ENDPOINT"; break;
		case TRB_TYPE_EVALUATE_CONTEXT: typeStr = "EVALUATE_CONTEXT"; break;
		case TRB_TYPE_RESET_ENDPOINT: typeStr = "RESET_ENDPOINT"; break;
		case TRB_TYPE_STOP_ENDPOINT: typeStr = "STOP_ENDPOINT"; break;
		case TRB_TYPE_SET_TR_DEQUEUE: typeStr = "SET_TR_DEQUEUE"; break;
		case TRB_TYPE_RESET_DEVICE: typeStr = "RESET_DEVICE"; break;
		case TRB_TYPE_FORCE_EVENT: typeStr = "FORCE_EVENT"; break;
		case TRB_TYPE_NEGOCIATE_BW: typeStr = "NEGOCIATE_BW"; break;
		case TRB_TYPE_SET_LATENCY_TOLERANCE: typeStr = "SET_LATENCY_TOLERANCE"; break;
		case TRB_TYPE_GET_PORT_BW: typeStr = "GET_PORT_BW"; break;
		case TRB_TYPE_FORCE_HEADER: typeStr = "FORCE_HEADER"; break;
		case TRB_TYPE_CMD_NOOP: typeStr = "CMD_NOOP"; break;
		case TRB_TYPE_TRANSFER: typeStr = "TRANSFER"; break;
		case TRB_TYPE_COMMAND_COMPLETION: typeStr = "COMMAND_COMPLETION"; break;
		case TRB_TYPE_PORT_STATUS_CHANGE: typeStr = "PORT_STATUS_CHANGE"; break;
		case TRB_TYPE_BANDWIDTH_REQUEST: typeStr = "BANDWIDTH_REQUEST"; break;
		case TRB_TYPE_DOORBELL: typeStr = "DOORBELL"; break;
		case TRB_TYPE_HOST_CONTROLLER: typeStr = "HOST_CONTROLLER"; break;
		case TRB_TYPE_DEVICE_NOTIFICATION: typeStr = "DEVICE_NOTIFICATION"; break;
		case TRB_TYPE_MFINDEX_WRAP: typeStr = "MFINDEX_WRAP"; break;
		case TRB_TYPE_NEC_COMMAND_COMPLETION: typeStr = "NEC_COMMAND_COMPLETION"; break;
		case TRB_TYPE_NEC_GET_FIRMWARE_REV: typeStr = "NEC_GET_FIRMWARE_REV"; break;
		default: typeStr = "?"; break;
	}
	return typeStr;
}


static const char*
xhci_trb_completion_string(uint32 trbType)
{
	const char* typeStr;
	switch (trbType) {
		case COMP_INVALID: typeStr = "INVALID"; break;
		case COMP_SUCCESS: typeStr = "SUCCESS"; break;
		case COMP_DATA_BUFFER: typeStr = "DATA_BUFFER"; break;
		case COMP_BABBLE: typeStr = "BABBLE"; break;
		case COMP_USB_TRANSACTION: typeStr = "USB_TRANSACTION"; break;
		case COMP_TRB: typeStr = "TRB"; break;
		case COMP_STALL: typeStr = "STALL"; break;
		case COMP_RESOURCE: typeStr = "RESOURCE"; break;
		case COMP_BANDWIDTH: typeStr = "BANDWIDTH"; break;
		case COMP_NO_SLOTS: typeStr = "NO_SLOTS"; break;
		case COMP_INVALID_STREAM: typeStr = "INVALID_STREAM"; break;
		case COMP_SLOT_NOT_ENABLED: typeStr = "SLOT_NOT_ENABLED"; break;
		case COMP_ENDPOINT_NOT_ENABLED: typeStr = "ENDPOINT_NOT_ENABLED"; break;
		case COMP_SHORT_PACKET: typeStr = "SHORT_PACKET"; break;
		case COMP_RING_UNDERRUN: typeStr = "RING_UNDERRUN"; break;
		case COMP_RING_OVERRUN: typeStr = "RING_OVERRUN"; break;
		case COMP_VF_RING_FULL: typeStr = "VF_RING_FULL"; break;
		case COMP_PARAMETER: typeStr = "PARAMETER"; break;
		case COMP_BANDWIDTH_OVERRUN: typeStr = "BANDWIDTH_OVERRUN"; break;
		case COMP_CONTEXT_STATE: typeStr = "CONTEXT_STATE"; break;
		case COMP_NO_PING_RESPONSE: typeStr = "NO_PING_RESPONSE"; break;
		case COMP_EVENT_RING_FULL: typeStr = "EVENT_RING_FULL"; break;
		case COMP_INCOMPATIBLE_DEVICE: typeStr = "INCOMPATIBLE_DEVICE"; break;
		case COMP_MISSED_SERVICE: typeStr = "MISSED_SERVICE"; break;
		case COMP_COMMAND_RING_STOPPED: typeStr = "COMMAND_RING_STOPPED"; break;
		case COMP_COMMAND_ABORTED: typeStr = "COMMAND_ABORTED"; break;
		case COMP_STOPPED: typeStr = "STOPPED"; break;
		case COMP_LENGTH_INVALID: typeStr = "LENGTH_INVALID"; break;
		case COMP_MAX_EXIT_LATENCY: typeStr = "MAX_EXIT_LATENCY"; break;
		case COMP_ISOC_OVERRUN: typeStr = "ISOC_OVERRUN"; break;
		case COMP_EVENT_LOST: typeStr = "EVENT_LOST"; break;
		case COMP_UNDEFINED: typeStr = "UNDEFINED"; break;
		case COMP_INVALID_STREAM_ID: typeStr = "INVALID_STREAM_ID"; break;
		case COMP_SECONDARY_BANDWIDTH: typeStr = "SECONDARY_BANDWIDTH"; break;
		case COMP_SPLIT_TRANSACTION: typeStr = "SPLIT_TRANSACTION"; break;
		default: typeStr = "?"; break;
	}
	return typeStr;
}


void
XhciRing::DumpTrb(const xhci_trb& trb)
{
	const char* typeStr = xhci_trb_type_string(TRB_3_TYPE_GET(trb.flags));
	switch (TRB_3_TYPE_GET(trb.flags)) {
		case TRB_TYPE_SETUP_STAGE: {
			usb_request_data requestData;
			memcpy(&requestData, &trb.address, sizeof(requestData));
			dprintf(
				"%s(bmRequestType: %" B_PRIu32
				", bRequest: %" B_PRIu32
				", wValue: %" B_PRIu32
				", wIndex: %" B_PRIu32
				", wLength: %" B_PRIu32
				", transferLen: %" B_PRIu32
				", irq: %" B_PRIu32
				", c: %d, ioc: %d, idt: %d, trt: %" B_PRIu32 ")\n",
				typeStr,
				requestData.RequestType,
				requestData.Request,
				requestData.Value,
				requestData.Index,
				requestData.Length,
				TRB_2_BYTES_GET(trb.status),
				TRB_2_IRQ_GET(trb.status),
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				((1U << TRB_3_IOC_BIT) & trb.flags) != 0,
				((1U << TRB_3_IDT_BIT) & trb.flags) != 0,
				(trb.flags >> 16) & 0x3);
			break;
		}
		case TRB_TYPE_LINK:
			dprintf("%s(address: %#" B_PRIx64 ", irq: %" B_PRId32 ", c: %d, tc: %d, ch: %d, ioc: %d)\n",
				typeStr,
				trb.address,
				TRB_2_IRQ_GET(trb.status),
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				((1U << TRB_3_TC_BIT) & trb.flags) != 0,
				((1U << TRB_3_CHAIN_BIT) & trb.flags) != 0,
				((1U << TRB_3_IOC_BIT) & trb.flags) != 0);
			break;
		case TRB_TYPE_CMD_NOOP:
		case TRB_TYPE_ENABLE_SLOT:
			dprintf("%s(c: %d)\n",
				typeStr,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0);
			break;
		case TRB_TYPE_DISABLE_SLOT:
		case TRB_TYPE_RESET_DEVICE:
			dprintf("%s(c: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_ADDRESS_DEVICE:
			dprintf("%s(c: %d, bsr: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				((1U << TRB_3_BSR_BIT) & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_CONFIGURE_ENDPOINT:
			dprintf("%s(c: %d, dc: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				((1U << TRB_3_DCEP_BIT) & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_EVALUATE_CONTEXT:
			dprintf("%s(inputCtx: %#" B_PRIx64 ", c: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				trb.address,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_RESET_ENDPOINT:
			dprintf("%s(c: %d, tsp: %d, endpoint: %" B_PRIu32 ", slot: %" B_PRIu32 ")\n",
				typeStr,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				((1U << TRB_3_PRSV_BIT) & trb.flags) != 0,
				TRB_3_ENDPOINT_GET(trb.flags),
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_STOP_ENDPOINT:
			dprintf("%s(c: %d, endpoint: %" B_PRIu32 ", sp: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				TRB_3_ENDPOINT_GET(trb.flags),
				((1U << TRB_3_SUSPEND_ENDPOINT_BIT) & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_SET_TR_DEQUEUE:
			dprintf("%s(address: %#" B_PRIx64 ", dcs: %d, sct: %" B_PRIu32 ", stream: %" B_PRIu32 ", c: %d, endpoint: %" B_PRIu32 ", slot: %" B_PRIu32 ")\n",
				typeStr,
				trb.address & ~((uint64)0xf),
				(ENDPOINT_2_DCS_BIT & trb.address) != 0,
				(uint32)((trb.address >> 1) & 0x7),
				TRB_2_STREAM_GET(trb.status),
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				TRB_3_ENDPOINT_GET(trb.flags),
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_TRANSFER:
			dprintf("%s(address: %#" B_PRIx64 ", completion: %s, transferLen: %" B_PRIu32 ", c: %d, ed: %d, endpoint: %" B_PRIu32 ", slot: %" B_PRIu32 ")\n",
				typeStr,
				trb.address,
				xhci_trb_completion_string(TRB_2_COMP_CODE_GET(trb.status)),
				TRB_2_REM_GET(trb.status),
				((1U << TRB_3_CYCLE_BIT) & trb.flags) != 0,
				((1U << TRB_3_EVENT_DATA_BIT) & trb.flags) != 0,
				TRB_3_ENDPOINT_GET(trb.flags),
				TRB_3_SLOT_GET(trb.flags));
			break;
		default:
			dprintf("%s(address: %#" B_PRIx64 ", status: %#" B_PRIx32 ", flags: %#" B_PRIx32 ")\n",
				typeStr, trb.address, trb.status, trb.flags);
			break;
	}
}


// #pragma mark - XhciTransferDesc

XhciTransferDesc::~XhciTransferDesc()
{
	if (fBuffers != NULL) {
		size_t totalSize = fBufferSize * fBufferCount;
		if (totalSize < (32 * B_PAGE_SIZE)) {
			// This was allocated as one contiguous buffer.
			fStack->FreeChunk(fBuffers[0], fBufferAddrs[0],
				totalSize);
		} else {
			for (uint32 i = 0; i < fBufferCount; i++) {
				if (fBuffers[i] == NULL)
					continue;
				fStack->FreeChunk(fBuffers[i], fBufferAddrs[i],
					fBufferSize);
			}
		}

		free(fBuffers);
	}
}


status_t
XhciTransferDesc::AllocBuffer(uint32 bufferCount, size_t bufferSize)
{
	if (bufferSize > 0) {
		// Due to how the USB stack allocates physical memory, we can't just
		// request one large chunk the size of the transfer, and so instead we
		// create a series of buffers as requested by our caller.

		// We store the buffer pointers and addresses in one memory block.
		fBuffers = (void**)calloc(bufferCount,
			(sizeof(void*) + sizeof(phys_addr_t)));
		if (fBuffers == NULL) {
			TRACE_ERROR("unable to allocate space for buffer infos\n");
			return B_NO_MEMORY;
		}
		fBufferAddrs = (phys_addr_t*)&fBuffers[bufferCount];
		fBufferSize = bufferSize;
		fBufferCount = bufferCount;

		// Optimization: If the requested total size of all buffers is less
		// than 32*B_PAGE_SIZE (the maximum size that the physical memory
		// allocator can handle), we allocate only one buffer and segment it.
		size_t totalSize = bufferSize * bufferCount;
		if (totalSize < (32 * B_PAGE_SIZE)) {
			if (fStack->AllocateChunk(&fBuffers[0],
					&fBufferAddrs[0], totalSize) < B_OK) {
				TRACE_ERROR("unable to allocate space for large buffer (size %ld)\n",
					totalSize);
				return B_NO_MEMORY;
			}
			for (uint32 i = 1; i < bufferCount; i++) {
				fBuffers[i] = (void*)((addr_t)(fBuffers[i - 1])
					+ bufferSize);
				fBufferAddrs[i] = fBufferAddrs[i - 1]
					+ bufferSize;
			}
		} else {
			// Otherwise, we allocate each buffer individually.
			for (uint32 i = 0; i < bufferCount; i++) {
				if (fStack->AllocateChunk(&fBuffers[i],
						&fBufferAddrs[i], bufferSize) < B_OK) {
					TRACE_ERROR("unable to allocate space for a buffer (size "
						"%" B_PRIuSIZE ", count %" B_PRIu32 ")\n",
						bufferSize, bufferCount);
					return B_NO_MEMORY;
				}
			}
		}
	} else {
		fBuffers = NULL;
		fBufferAddrs = NULL;
	}

	return B_OK;
}


status_t
XhciTransferDesc::FillTransfer(XHCI& xhci, XhciRing& ring)
{
	UsbBusPipe* pipe = fTransfer->TransferPipe();

	fBegin = ring.EnqueueRd();
	fEnd = ring.EnqueueRd();

	CHECK_RET(fTransfer->InitKernelAccess());

	if (pipe->Type() == USB_PIPE_CONTROL) {
		CHECK_RET(FillControlTransfer(xhci, ring));
	} else {
		CHECK_RET(FillNormalTransfer(xhci, ring));
	}

	return B_OK;
}


status_t
XhciTransferDesc::FillControlTransfer(XHCI& xhci, XhciRing& ring)
{
	usb_request_data* requestData = fTransfer->RequestData();
	bool directionIn = (requestData->RequestType & USB_REQTYPE_DEVICE_IN) != 0;

	CHECK_RET(AllocBuffer(1, requestData->Length));

	XhciRingRider rd = fEnd;
	CHECK_RET(ring.Alloc(fEnd, false));

	// Setup Stage
	memcpy(&rd.trb->address, requestData, sizeof(usb_request_data));
	rd.trb->status
		= TRB_2_IRQ(0)
		| TRB_2_BYTES(8);
	rd.trb->flags
		= TRB_3_TYPE(TRB_TYPE_SETUP_STAGE)
		| (1U << TRB_3_IDT_BIT)
		| ((uint32)(!rd.cycleBit) << TRB_3_CYCLE_BIT);
	if (requestData->Length > 0)
		rd.trb->flags |= directionIn ? TRB_3_TRT_IN : TRB_3_TRT_OUT;

	// Data Stage (if any)
	if (requestData->Length > 0) {
		rd = fEnd;
		CHECK_RET(ring.Alloc(fEnd, true));

		*rd.trb = {
			.address = fBufferAddrs[0],
			.status
				= (uint32)TRB_2_IRQ(0)
				| TRB_2_BYTES(requestData->Length)
				| TRB_2_TD_SIZE(0),
			.flags
				= TRB_3_TYPE(TRB_TYPE_DATA_STAGE)
				| (1U << TRB_3_ISP_BIT)
				| (directionIn ? TRB_3_DIR_IN : 0)
				| ((uint32)rd.cycleBit << TRB_3_CYCLE_BIT)
		};

		if (!directionIn) {
			CHECK_RET(fTransfer->PrepareKernelAccess());
			Write(fTransfer->Vector(), fTransfer->VectorCount(), fTransfer->IsPhysical());
		}
	}

	rd = fEnd;
	CHECK_RET(ring.Alloc(fEnd, false));

	// Status Stage
	rd.trb->address = 0;
	rd.trb->status = TRB_2_IRQ(0);
	rd.trb->flags
		= TRB_3_TYPE(TRB_TYPE_STATUS_STAGE)
		| (1U << TRB_3_IOC_BIT)
		| ((uint32)rd.cycleBit << TRB_3_CYCLE_BIT);
		// The CHAIN bit must be set when using an Event Data TRB
		// (XHCI 1.2 § 6.4.1.2.3 Table 6-31 p472).

	// Status Stage is an OUT transfer when the device is sending data
	// (XHCI 1.2 § 4.11.2.2 Table 4-7 p213), otherwise set the IN bit.
	if (requestData->Length == 0 || !directionIn)
		rd.trb->flags |= TRB_3_DIR_IN;

	return B_OK;
}


status_t
XhciTransferDesc::FillNormalTransfer(XHCI& xhci, XhciRing& ring)
{
	TRACE("SubmitNormalRequest() length %" B_PRIuSIZE "\n", fTransfer->FragmentLength());

	UsbBusPipe* pipe = fTransfer->TransferPipe();
	usb_isochronous_data* isochronousData = fTransfer->IsochronousData();
	UsbBusPipe::pipeDirection direction = pipe->Direction();
	XhciEndpoint* endpoint = (XhciEndpoint*)pipe->ControllerCookie();

	XhciRingRider rd = fEnd;

	if (isochronousData != NULL) {
		if (isochronousData->packet_count == 0)
			return B_BAD_VALUE;

		// Isochronous transfers use more specifically sized packets.
		const size_t trbSize = fTransfer->DataLength() / isochronousData->packet_count;
		const int32 trbCount = isochronousData->packet_count;
		if (trbSize == 0 || trbSize > pipe->MaxPacketSize()
				|| trbSize != (size_t)isochronousData->packet_descriptors[0].request_length)
			return B_BAD_VALUE;

		CHECK_RET(AllocBuffer(trbCount, trbSize));

		uint32 frame;
		if ((isochronousData->flags & USB_ISO_ASAP) != 0
				|| isochronousData->starting_frame_number == NULL) {
			frame = (xhci.ReadRunReg32(XHCI_MFINDEX) >> 3) + 1;
		} else {
			frame = *isochronousData->starting_frame_number;
		}

		for (int32 i = 0; i < trbCount; i++) {
			rd = fEnd;
			CHECK_RET(ring.Alloc(fEnd, false));

			*rd.trb = {
				.address = fBufferAddrs[i],
				.status
					= (uint32)TRB_2_REM(isochronousData->packet_descriptors[i].request_length)
					| TRB_2_IRQ(0),
				.flags
					= TRB_3_TYPE(TRB_TYPE_ISOCH)
					| ((uint32)(rd.cycleBit != (i == 0)) << TRB_3_CYCLE_BIT)
					| TRB_3_FRID(frame)
					| (1U << TRB_3_IOC_BIT)
			};

			frame = (frame + 1) % 2048;
		}
		if (isochronousData->starting_frame_number != NULL)
			*isochronousData->starting_frame_number = frame;

		// TODO: The OHCI bus driver seems to also do this for inbound
		// isochronous transfers. Perhaps it should be moved into the stack?
		if (direction == UsbBusPipe::In) {
			for (uint32 i = 0; i < isochronousData->packet_count; i++) {
				isochronousData->packet_descriptors[i].actual_length = 0;
				isochronousData->packet_descriptors[i].status = B_NO_INIT;
			}
		}
	} else {
		// TRBs within a TD must be "grouped" into TD Fragments, which mostly means
		// that a max_burst_payload boundary cannot be crossed within a TRB, but
		// only between TRBs. More than one TRB can be in a TD Fragment, but we keep
		// things simple by setting trbSize to the MBP. (XHCI 1.2 § 4.11.7.1 p235.)
		size_t trbSize = endpoint->fMaxBurstPayload;

		// Now that we know trbSize, compute the count.
		const int32 trbCount = (fTransfer->FragmentLength() + trbSize - 1) / trbSize;

		CHECK_RET(AllocBuffer(trbCount, trbSize));

		const size_t maxPacketSize = pipe->MaxPacketSize();
		size_t remaining = fTransfer->FragmentLength();
		for (int32 i = 0; i < trbCount; i++) {
			int32 trbLength = std::min<int32>(remaining, trbSize);
			remaining -= trbLength;

			// The "TD Size" field of a transfer TRB indicates the number of
			// remaining maximum-size *packets* in this TD, *not* including the
			// packets in the current TRB, and capped at 31 if there are more
			// than 31 packets remaining in the TD. (XHCI 1.2 § 4.11.2.4 p218.)
			int32 tdSize = std::min<int32>((remaining + maxPacketSize - 1) / maxPacketSize, 31);

			rd = fEnd;
			CHECK_RET(ring.Alloc(fEnd, i != 0));

			*rd.trb = {
				.address = fBufferAddrs[i],
				.status
					= (uint32)TRB_2_REM(trbLength)
					| TRB_2_TD_SIZE(tdSize)
					| TRB_2_IRQ(0),
				.flags
					= TRB_3_TYPE(TRB_TYPE_NORMAL)
					| (1U << TRB_3_CHAIN_BIT)
					| (1U << TRB_3_ISP_BIT)
					| ((uint32)(rd.cycleBit != (i == 0)) << TRB_3_CYCLE_BIT)
			};
		}

		rd.trb->flags &= ~(1U << TRB_3_CHAIN_BIT);
		rd.trb->flags |= (1U << TRB_3_IOC_BIT);
	}

	if (direction == UsbBusPipe::Out) {
		CHECK_RET(fTransfer->PrepareKernelAccess());
		Write(fTransfer->Vector(), fTransfer->VectorCount(), fTransfer->IsPhysical());
	}

	return B_OK;
}


void
XhciTransferDesc::DPCCallback::DoDPC(DPCQueue* queue)
{
	TRACE("finishing transfer td %p\n", &Base());

	UsbBusTransfer* transfer = Base().fTransfer;
	UsbBusPipe* pipe = transfer->TransferPipe();
	XhciEndpoint* endpoint = (XhciEndpoint *)pipe->ControllerCookie();
	XHCI* xhci = endpoint->fDevice->fBase;
	bool directionIn = (transfer->TransferPipe()->Direction() != UsbBusPipe::Out);

	status_t callbackStatus = Base().fCompletionStatus;
	size_t expectedLength = transfer->FragmentLength();
	size_t actualLength = Base().fTransferred;

	if (directionIn && actualLength > 0) {
		TRACE("copying in iov count %ld\n", fTransfer->VectorCount());
		status_t status = transfer->PrepareKernelAccess();
		if (status == B_OK) {
			Base().Read(transfer->Vector(), transfer->VectorCount(),
				transfer->IsPhysical());
		} else {
			callbackStatus = status;
		}
	}

	delete &Base();

	// this transfer may still have data left
	bool finished = true;
	transfer->AdvanceByFragment(actualLength);
	if (expectedLength == actualLength && transfer->FragmentLength() > 0) {
		TRACE("still %" B_PRIuSIZE " bytes left on transfer\n",
			fTransfer->FragmentLength());
		callbackStatus = xhci->SubmitTransfer(transfer);
		finished = (callbackStatus != B_OK);
	}
	if (finished) {
		// The actualLength was already handled in AdvanceByFragment.
		transfer->Finished(callbackStatus, 0);
		transfer->Free();
	}
}
