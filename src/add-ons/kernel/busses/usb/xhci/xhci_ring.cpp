#include "xhci.h"

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
			.flags = cycleBit ? TRB_3_CYCLE_BIT : 0
		};
	}
	xhci_trb* linkTrb = fTrbs + kMaxUsableLength;
	*linkTrb = {
		.flags =
			TRB_3_TYPE(TRB_TYPE_LINK) |
			(cycleBit ? TRB_3_CYCLE_BIT : 0)
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
	lastSegment->LinkTrb()->flags |= TRB_3_TC_BIT;

	return B_OK;
}


status_t
XhciRing::Alloc(XhciRingRider& rd, bool chain)
{
	rd.Inc();
	if (!rd.IsLink()) {
		return B_OK;
	}

	if (rd.cycleBit)
		rd.seg->LinkTrb()->flags |= TRB_3_CYCLE_BIT;
	else
		rd.seg->LinkTrb()->flags &= ~((uint32)TRB_3_CYCLE_BIT);

	if (chain)
		rd.seg->LinkTrb()->flags |= TRB_3_CHAIN_BIT;

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

	fEnqueue.trb->flags ^= TRB_3_CYCLE_BIT;
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
	if ((eventTrb.flags & TRB_3_EVENT_DATA_BIT) == 0) {
		TRACE_ERROR("got an interrupt for a non-Event Data TRB!\n");
		// TODO: Handle error
		return;
	}

	const uint8 completionCode = TRB_2_COMP_CODE_GET(eventTrb.status);
	int32 transferred = TRB_2_REM_GET(eventTrb.status);
	int32 remainder = -1;
	const phys_addr_t source = eventTrb.address;

	XhciTransferDesc* td = LookupTransferDesc(source);
	if (td == NULL)
		panic("TD referenced in completion event not found in the ring\n");

	td->fTrbCompletionCode = completionCode;
	td->fTdTransferred = transferred;
	td->fTrbLeft = remainder;

	Complete(td->fEnd);
	fTransferDescs.Remove(td);

	locker.Unlock();
	xhci.CompleteTransferDesc(td);
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


void
XhciRing::DumpTrb(xhci_trb& trb)
{
	const char* typeStr;
	switch (TRB_3_TYPE_GET(trb.flags)) {
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
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				(TRB_3_IOC_BIT & trb.flags) != 0,
				(TRB_3_IDT_BIT & trb.flags) != 0,
				(trb.flags >> 16) & 0x3);
			break;
		}
		case TRB_TYPE_LINK:
			dprintf("%s(address: %#" B_PRIx64 ", irq: %" B_PRId32 ", c: %d, tc: %d, ch: %d, ioc: %d)\n",
				typeStr,
				trb.address,
				TRB_2_IRQ_GET(trb.status),
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				(TRB_3_TC_BIT & trb.flags) != 0,
				(TRB_3_CHAIN_BIT & trb.flags) != 0,
				(TRB_3_IOC_BIT & trb.flags) != 0);
			break;
		case TRB_TYPE_CMD_NOOP:
		case TRB_TYPE_ENABLE_SLOT:
			dprintf("%s(c: %d)\n",
				typeStr,
				(TRB_3_CYCLE_BIT & trb.flags) != 0);
			break;
		case TRB_TYPE_DISABLE_SLOT:
		case TRB_TYPE_RESET_DEVICE:
			dprintf("%s(c: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_ADDRESS_DEVICE:
			dprintf("%s(c: %d, bsr: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				(TRB_3_BSR_BIT & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_CONFIGURE_ENDPOINT:
			dprintf("%s(c: %d, dc: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				(TRB_3_DCEP_BIT & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_EVALUATE_CONTEXT:
			dprintf("%s(inputCtx: %#" B_PRIx64 ", c: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				trb.address,
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_RESET_ENDPOINT:
			dprintf("%s(c: %d, tsp: %d, endpoint: %" B_PRIu32 ", slot: %" B_PRIu32 ")\n",
				typeStr,
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				(TRB_3_PRSV_BIT & trb.flags) != 0,
				TRB_3_ENDPOINT_GET(trb.flags),
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_STOP_ENDPOINT:
			dprintf("%s(c: %d, endpoint: %" B_PRIu32 ", sp: %d, slot: %" B_PRIu32 ")\n",
				typeStr,
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
				TRB_3_ENDPOINT_GET(trb.flags),
				(TRB_3_SUSPEND_ENDPOINT_BIT & trb.flags) != 0,
				TRB_3_SLOT_GET(trb.flags));
			break;
		case TRB_TYPE_SET_TR_DEQUEUE:
			dprintf("%s(address: %#" B_PRIx64 ", dcs: %d, sct: %" B_PRIu32 ", stream: %" B_PRIu32 ", c: %d, endpoint: %" B_PRIu32 ", slot: %" B_PRIu32 ")\n",
				typeStr,
				trb.address & ~((uint64)0xf),
				(ENDPOINT_2_DCS_BIT & trb.address) != 0,
				(uint32)((trb.address >> 1) & 0x7),
				TRB_2_STREAM_GET(trb.status),
				(TRB_3_CYCLE_BIT & trb.flags) != 0,
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
	UsbBusPipe *pipe = fTransfer->TransferPipe();

	fBegin = ring.EnqueueRd();
	fEnd = ring.EnqueueRd();

	if (pipe->Type() == USB_PIPE_CONTROL) {
		CHECK_RET(FillControlTransfer(xhci, ring));
	} else {
		CHECK_RET(FillNormalTransfer(xhci, ring));
	}

	XhciRingRider rd = fEnd;
	CHECK_RET(ring.Alloc(fEnd, true));

	rd.trb->address = fBegin.PhysAddr();
	rd.trb->status = xhci_trb_status{.irq_target = 0}.value;
	rd.trb->flags =
		TRB_3_TYPE(TRB_TYPE_EVENT_DATA) |
		TRB_3_IOC_BIT |
		(rd.cycleBit ? TRB_3_CYCLE_BIT : 0);

	return B_OK;
}


status_t
XhciTransferDesc::FillControlTransfer(XHCI& xhci, XhciRing& ring)
{
	usb_request_data *requestData = fTransfer->RequestData();
	bool directionIn = (requestData->RequestType & USB_REQTYPE_DEVICE_IN) != 0;

	CHECK_RET(fTransfer->InitKernelAccess());

	CHECK_RET(AllocBuffer(1, requestData->Length));

	XhciRingRider rd = fEnd;
	CHECK_RET(ring.Alloc(fEnd, true));

	// Setup Stage
	memcpy(&rd.trb->address, requestData, sizeof(usb_request_data));
	rd.trb->status =
		TRB_2_IRQ(0) |
		TRB_2_BYTES(8);
	rd.trb->flags =
		TRB_3_TYPE(TRB_TYPE_SETUP_STAGE) |
		TRB_3_IDT_BIT |
		(rd.cycleBit ? 0 : TRB_3_CYCLE_BIT);
	if (requestData->Length > 0)
		rd.trb->flags |= directionIn ? TRB_3_TRT_IN : TRB_3_TRT_OUT;

	// Data Stage (if any)
	if (requestData->Length > 0) {
		rd = fEnd;
		CHECK_RET(ring.Alloc(fEnd, true));

		rd.trb->address = fBufferAddrs[0];
		rd.trb->status =
			TRB_2_IRQ(0) |
			TRB_2_BYTES(requestData->Length) |
			TRB_2_TD_SIZE(0);
		rd.trb->flags =
			TRB_3_TYPE(TRB_TYPE_DATA_STAGE) |
			(directionIn ? TRB_3_DIR_IN : 0) |
			(rd.cycleBit ? TRB_3_CYCLE_BIT : 0);

		if (!directionIn) {
			fTransfer->PrepareKernelAccess();
			Write(fTransfer->Vector(), fTransfer->VectorCount(), fTransfer->IsPhysical());
		}
	}

	rd = fEnd;
	CHECK_RET(ring.Alloc(fEnd, true));

	// Status Stage
	rd.trb->address = 0;
	rd.trb->status = TRB_2_IRQ(0);
	rd.trb->flags =
		TRB_3_TYPE(TRB_TYPE_STATUS_STAGE) |
		TRB_3_CHAIN_BIT |
		TRB_3_ENT_BIT |
		(rd.cycleBit ? TRB_3_CYCLE_BIT : 0);
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

	UsbBusPipe *pipe = fTransfer->TransferPipe();
	usb_isochronous_data *isochronousData = fTransfer->IsochronousData();
	bool directionIn = (pipe->Direction() == UsbBusPipe::In);

	XhciEndpoint *endpoint = (XhciEndpoint *)pipe->ControllerCookie();

	CHECK_RET(fTransfer->InitKernelAccess());

	// TRBs within a TD must be "grouped" into TD Fragments, which mostly means
	// that a max_burst_payload boundary cannot be crossed within a TRB, but
	// only between TRBs. More than one TRB can be in a TD Fragment, but we keep
	// things simple by setting trbSize to the MBP. (XHCI 1.2 § 4.11.7.1 p235.)
	size_t trbSize = endpoint->fMaxBurstPayload;

	if (isochronousData != NULL) {
		if (isochronousData->packet_count == 0)
			return B_BAD_VALUE;

		// Isochronous transfers use more specifically sized packets.
		trbSize = fTransfer->DataLength() / isochronousData->packet_count;
		if (trbSize == 0 || trbSize > pipe->MaxPacketSize() || trbSize
				!= (size_t)isochronousData->packet_descriptors[0].request_length)
			return B_BAD_VALUE;
	}

	// Now that we know trbSize, compute the count.
	const int32 trbCount = (fTransfer->FragmentLength() + trbSize - 1) / trbSize;

	CHECK_RET(AllocBuffer(trbCount, trbSize));

	XhciRingRider rd = fEnd;

	// Normal Stage
	const size_t maxPacketSize = pipe->MaxPacketSize();
	size_t remaining = fTransfer->FragmentLength();
	for (int32 i = 0; i < trbCount; i++) {
		int32 trbLength = (remaining < trbSize) ? remaining : trbSize;
		remaining -= trbLength;

		// The "TD Size" field of a transfer TRB indicates the number of
		// remaining maximum-size *packets* in this TD, *not* including the
		// packets in the current TRB, and capped at 31 if there are more
		// than 31 packets remaining in the TD. (XHCI 1.2 § 4.11.2.4 p218.)
		int32 tdSize = (remaining + maxPacketSize - 1) / maxPacketSize;
		if (tdSize > 31)
			tdSize = 31;

		rd = fEnd;
		CHECK_RET(ring.Alloc(fEnd, true));

		rd.trb->address = fBufferAddrs[i];
		rd.trb->status = xhci_trb_status{
			.transfer_length = (uint32)trbLength,
			.td_size = (uint32)tdSize,
			.irq_target = 0
		}.value;
		rd.trb->flags = xhci_trb_flags{
			.cycle = i == 0 ? !rd.cycleBit : rd.cycleBit,
			.chain = true,
			.trb_type = TRB_TYPE_NORMAL
		}.value;
	}

	// Isochronous-specific
	if (isochronousData != NULL) {
		// This is an isochronous transfer; we need to make the first TRB
		// an isochronous TRB.
		fBegin.trb->flags &= ~(TRB_3_TYPE(TRB_TYPE_NORMAL));
		fBegin.trb->flags |= TRB_3_TYPE(TRB_TYPE_ISOCH);

		// Isochronous pipes are scheduled by microframes, one of which
		// is 125us for USB 2 and above. But for USB 1 it was 1ms, so
		// we need to use a different frame delta for that case.
		uint8 frameDelta = 1;
		if (fTransfer->TransferPipe()->Speed() == USB_SPEED_FULLSPEED)
			frameDelta = 8;

		// TODO: We do not currently take Mult into account at all!
		// How are we supposed to do that here?

		// Determine the (starting) frame number: if ISO_ASAP is set,
		// we are queueing this "right away", and so want to reset
		// the starting_frame_number. Otherwise we use the passed one.
		uint32 frame;
		if ((isochronousData->flags & USB_ISO_ASAP) != 0
				|| isochronousData->starting_frame_number == NULL) {
			// All reads from the microframe index register must be
			// incremented by 1. (XHCI 1.2 § 4.14.2.1.4 p265.)
			frame = xhci.ReadRunReg32(XHCI_MFINDEX) + 1;
			fBegin.trb->flags |= TRB_3_ISO_SIA_BIT;
		} else {
			frame = *isochronousData->starting_frame_number;
			fBegin.trb->flags |= TRB_3_FRID(frame);
		}
		frame = (frame + frameDelta) % 2048;
		if (isochronousData->starting_frame_number != NULL)
			*isochronousData->starting_frame_number = frame;

		// TODO: The OHCI bus driver seems to also do this for inbound
		// isochronous transfers. Perhaps it should be moved into the stack?
		if (directionIn) {
			for (uint32 i = 0; i < isochronousData->packet_count; i++) {
				isochronousData->packet_descriptors[i].actual_length = 0;
				isochronousData->packet_descriptors[i].status = B_NO_INIT;
			}
		}
	}

	// Set the ENT (Evaluate Next TRB) bit, so that the HC will not switch
	// contexts before evaluating the Link TRB that _LinkDescriptorForPipe
	// will insert, as otherwise there would be a race between us freeing
	// and unlinking the descriptor, and the controller evaluating the Link TRB
	// and thus getting back onto the main ring and executing the Event Data
	// TRB that generates the interrupt for this transfer.
	//
	// Note that we *do not* unset the CHAIN bit in this TRB, thus including
	// the Link TRB in this TD formally, which is required when using the
	// ENT bit. (XHCI 1.2 § 4.12.3 p250.)
	rd.trb->flags |= TRB_3_ENT_BIT;

	return B_OK;
}
