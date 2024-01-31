#pragma once

#include <optional>

#include <dm2/bus/USB.h>
#include <dm2/bus/PCI.h>

#include <AutoDeleterOS.h>
#include <ContainerOf.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <util/iovec_support.h>

#include <DPC.h>
#include <condition_variable.h>

#include "usbspec_private.h"
#include "xhci_hardware.h"


/*
#define TRACE_OUTPUT(x, y, z...) \
	{ \
		dprintf("usb %s%s %" B_PRId32 ": ", y, (x)->TypeName(), (x)->USBID()); \
		dprintf(z); \
	}
*/

#define TRACE_OUTPUT(x, y, z...) dprintf("xhci: " z)

//#define TRACE_USB
#ifdef TRACE_USB
#define TRACE(x...)					TRACE_OUTPUT(this, "", x)
#define TRACE_STATIC(x, y...)		TRACE_OUTPUT(x, "", y)
#define TRACE_MODULE(x...)			dprintf("usb " USB_MODULE_NAME ": " x)
#else
#define TRACE(x...)					/* nothing */
#define TRACE_STATIC(x, y...)		/* nothing */
#define TRACE_MODULE(x...)			/* nothing */
#endif

#define TRACE_ALWAYS(x...)			TRACE_OUTPUT(this, "", x)
#define TRACE_ERROR(x...)			TRACE_OUTPUT(this, "error ", x)
#define TRACE_MODULE_ALWAYS(x...)	dprintf("usb " USB_MODULE_NAME ": " x)
#define TRACE_MODULE_ERROR(x...)	dprintf("usb " USB_MODULE_NAME ": " x)


class XhciRing;
class XhciTransferDesc;
class XhciDevice;
class XhciEndpoint;
class XHCI;


class XhciRingSegment {
public:
	status_t Init(bool cycleBit);
	~XhciRingSegment();

	xhci_trb* LinkTrb() const {return fTrbs + kMaxUsableLength;}

	void SetNext(XhciRingSegment* nextSeg)
	{
		fNext = nextSeg;
		LinkTrb()->address = nextSeg->fTrbAddr;
	}

public:
	static const uint32 kMaxLength = (B_PAGE_SIZE / sizeof(xhci_trb));
	static const uint32 kMaxUsableLength = kMaxLength - 1;

	XhciRingSegment* fNext {};
	xhci_trb* fTrbs {};
	phys_addr_t fTrbAddr {};
};


struct XhciRingRider {
	XhciRingSegment* seg;
	xhci_trb* trb;
	bool cycleBit;

	XhciRingRider(): seg(NULL), trb(NULL), cycleBit(true) {}
	XhciRingRider(XhciRingSegment* seg): seg(seg), trb(seg->fTrbs), cycleBit(true) {}
	XhciRingRider(XhciRingSegment* seg, xhci_trb* trb): seg(seg), trb(trb), cycleBit(true) {}

	phys_addr_t PhysAddr() const
	{
		return seg->fTrbAddr + ((uint8*)trb - (uint8*)seg->fTrbs);
	}

	bool operator==(const XhciRingRider& other) const
	{
		return trb == other.trb;
	}

	bool operator!=(const XhciRingRider& other) const
	{
		return trb != other.trb;
	}

	bool IsLink() const
	{
		return (trb == seg->fTrbs + XhciRingSegment::kMaxLength - 1);
	}

	void Inc()
	{
		if (IsLink()) {
			if ((seg->LinkTrb()->flags & (1U << TRB_3_TC_BIT)) != 0)
				cycleBit = !cycleBit;

			seg = seg->fNext;
			trb = seg->fTrbs;
		} else {
			trb++;
		}
	}

	void IncSkipLinks()
	{
		Inc();
		if (IsLink())
			Inc();
	}

	int32 Substract(XhciRingRider rd, bool skipLinks) const
	{
		int32 res = 0;
		while (rd.seg != seg) {
			res += XhciRingSegment::kMaxLength - (rd.trb - seg->fTrbs);
			if (skipLinks)
				res -= 1;

			rd.seg = rd.seg->fNext;
		}
		res += rd.trb - rd.seg->fTrbs;
		if (skipLinks && res != 0 && rd.IsLink())
			res -= 1;

		return res;
	}
};


class XhciTransferDesc {
public:
	XhciTransferDesc(UsbStack* stack): fStack(stack) {}
	~XhciTransferDesc();

	size_t Read(generic_io_vec* vector, size_t vectorCount, bool physical);
	size_t Write(generic_io_vec* vector, size_t vectorCount, bool physical);

	status_t AllocBuffer(uint32 bufferCount, size_t bufferSize);

	status_t FillTransfer(XHCI& xhci, XhciRing& ring);
	status_t FillControlTransfer(XHCI& xhci, XhciRing& ring);
	status_t FillNormalTransfer(XHCI& xhci, XhciRing& ring);

public:
	UsbStack*	fStack;

	XhciRingRider fBegin;
	XhciRingRider fEnd;

	void**		fBuffers {};
	phys_addr_t* fBufferAddrs {};
	size_t		fBufferSize {};
	uint32		fBufferCount {};

	UsbBusTransfer*	fTransfer {};
	status_t	fCompletionStatus = B_OK;
	int32		fTransferred {};

	DoublyLinkedListLink<XhciTransferDesc>
				fLink;

	class DPCCallback: public ::DPCCallback {
	public:
		XhciTransferDesc& Base() {return ContainerOf(*this, &XhciTransferDesc::fDpcCallback);}

		void DoDPC(DPCQueue* queue) final;
	} fDpcCallback;

public:
	typedef DoublyLinkedList<
		XhciTransferDesc,
		DoublyLinkedListMemberGetLink<XhciTransferDesc, &XhciTransferDesc::fLink>
	> List;
};


class XhciRing {
public:
	~XhciRing();
	status_t Init(uint32 segmentCount);

	status_t Alloc(XhciRingRider& rd, bool chain);
	void Commit(const XhciRingRider& newEnqueue);
	void Complete(const XhciRingRider& newDequeue) {fDequeue = newDequeue;}

	const XhciRingRider& EnqueueRd() const {return fEnqueue;}
	const XhciRingRider& DequeueRd() const {return fDequeue;}

	status_t SubmitTransfer(XHCI& xhci, UsbBusTransfer* transfer);
	void CompleteTransfer(XHCI& xhci, MutexLocker& locker, const xhci_trb& eventTrb);
	status_t CancelAllTransfers(XHCI& xhci, MutexLocker& locker, XhciEndpoint* endpoint);

	static void DumpTrb(const xhci_trb& trb);

private:
	XhciTransferDesc* LookupTransferDesc(phys_addr_t addr);
	XhciTransferDesc* LookupTransferDescTrb(phys_addr_t addr, int32& tdIndex, size_t& completedLen);

private:
	XhciRingRider fEnqueue;
	XhciRingRider fDequeue;

	XhciTransferDesc::List fTransferDescs;
};


class XhciEndpoint {
public:
	XhciEndpoint(XhciDevice* device, uint8 id): fDevice(device), fId(id) {}
	status_t Configure(uint8 type,
		bool directionIn, uint16 interval, uint16 maxPacketSize, usb_speed speed,
		uint8 maxBurst, uint16 bytesPerInterval);

public:
	mutex 			fLock = MUTEX_INITIALIZER("xhci endpoint lock");

	XhciDevice*		fDevice {};
	uint8			fId {};

	uint16			fMaxBurstPayload {};

	XhciRing		fRing;
};


class XhciDevice {
public:
	XhciDevice(XHCI* base, uint8 slot): fBase(base), fSlot(slot) {}
	~XhciDevice();

public:
	XHCI* fBase;

	uint8 fSlot;
	uint8 fAddress {};
	bool fIsMultiTt {};

	AreaDeleter fInputCtxArea;
	phys_addr_t fInputCtxAddr {};
	struct xhci_input_device_ctx* fInputCtx {};

	AreaDeleter fDeviceCtxArea;
	phys_addr_t fDeviceCtxAddr {};
	struct xhci_device_ctx* fDeviceCtx {};

	std::optional<XhciEndpoint> fEndpoints[XHCI_MAX_ENDPOINTS - 1];
};


class XHCIRootHub {
public:
		status_t				Init(UsbBusManager *busManager);

								XHCIRootHub(XHCI* xhci): fXhci(xhci) {}
virtual							~XHCIRootHub();

		UsbBusDevice*			GetDevice() const {return fDevice;}
		uint8					AddPort(uint32 xhciPort);
		uint8					GetXHCIPort(uint32 portNo) {return fPorts[portNo - 1];}

virtual	bool					IsUsb3() = 0;
virtual	status_t				ProcessTransfer(UsbBusTransfer *transfer);

		void					PortStatusChanged(uint32 portNo);

private:
		void					TryCompleteInterruptTransfer(MutexLocker& lock);

protected:
	mutex fLock = MUTEX_INITIALIZER("XHCIRootHub");
	XHCI *fXhci;
	UsbBusDevice* fDevice {};

	uint8 fPortCount {};
	uint8 fPorts[USB_MAX_PORT_COUNT] {};

	UsbBusTransfer* fInterruptTransfer {};
	bool fHasChangedPorts {};
	uint8 fChangedPorts[USB_MAX_PORT_COUNT / 8] {};
};


class XHCI2RootHub: public XHCIRootHub {
public:
								XHCI2RootHub(XHCI* xhci): XHCIRootHub(xhci) {}
virtual							~XHCI2RootHub() = default;

		bool					IsUsb3() final { return false; };
		status_t				ProcessTransfer(UsbBusTransfer *transfer) final;
};


class XHCI3RootHub: public XHCIRootHub {
public:
								XHCI3RootHub(XHCI* xhci): XHCIRootHub(xhci) {}
virtual							~XHCI3RootHub() = default;

		bool					IsUsb3() final { return true; };
		status_t				ProcessTransfer(UsbBusTransfer *transfer) final;
};


class XHCI: public DeviceDriver, public UsbHostController {
public:
					XHCI(DeviceNode* node):
						fNode(node),
						fRootHub2(this), fRootHub3(this),
						fBusManagerDriver(*this) {}
	virtual 		~XHCI();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	// UsbHostController
	void			SetBusManager(UsbStack* stack, UsbBusManager* busManager) final;

	UsbBusDevice*	AllocateDevice(UsbBusDevice* parent,
								int8 hubAddress, uint8 hubPort,
								usb_speed speed) final;
	void			FreeDevice(UsbBusDevice* device) final;

	status_t		InitDevice(UsbBusDevice* device, const usb_device_descriptor& deviceDescriptor) final;
	status_t		InitHub(UsbBusDevice* device, const usb_hub_descriptor& hubDescriptor) final;

	status_t		Start() final;
	status_t		Stop() final;

	status_t		StartDebugTransfer(UsbBusTransfer* transfer) final;
	status_t		CheckDebugTransfer(UsbBusTransfer* transfer) final;
	void			CancelDebugTransfer(UsbBusTransfer* transfer) final;

	status_t		SubmitTransfer(UsbBusTransfer* transfer) final;
	status_t		CancelQueuedTransfers(UsbBusPipe* pipe, bool force) final;

	status_t		NotifyPipeChange(UsbBusPipe* pipe, usb_change change) final;

	const char*		TypeName() const final { return "xhci"; }

			status_t			SubmitControlRequest(UsbBusTransfer *transfer);
			status_t			SubmitNormalRequest(UsbBusTransfer *transfer);

			// Port operations for root hub
			uint8				PortCount() const { return fPortCount; }
			usb_speed			GetPortProtocol(uint8 index) const { return fPortSpeeds[index]; }
			status_t			GetPortStatus(uint8 index,
									usb_port_status *status);
			status_t			SetPortFeature(uint8 index, uint16 feature);
			status_t			ClearPortFeature(uint8 index, uint16 feature);

			status_t			GetPortSpeed(uint8 index, usb_speed *speed);

private:
			void				DumpEndpointState(xhci_endpoint_ctx& endpoint);

	inline	bool				Lock() { return fBusManager->Lock(); }
	inline	void				Unlock() { return fBusManager->Unlock(); }

			status_t			Init();

			void				BuildRoute(UsbBusDevice* hub, uint8 hubPort, uint8& rhPort, uint32& route);

			// Controller resets
			status_t			ControllerReset();
			status_t			ControllerHalt();

			// Interrupt functions
	static	int32				InterruptHandler(void *data);
			int32				Interrupt();

			// Endpoint management
			status_t			ConfigureEndpoint(XhciEndpoint* ep, uint8 slot,
									uint8 number, uint8 type, bool directionIn,
									uint16 interval, uint16 maxPacketSize,
									usb_speed speed, uint8 maxBurst,
									uint16 bytesPerInterval);
			uint8				_GetEndpointState(XhciEndpoint* ep);

			status_t			_InsertEndpointForPipe(UsbBusPipe *pipe);
			status_t			_RemoveEndpointForPipe(UsbBusPipe *pipe);

			// Event management
	static	int32				EventThread(void *data);
			void				CompleteEvents();
			void				ProcessEvents();

			// Descriptor management
			XhciTransferDesc *			CreateDescriptor(uint32 trbCount,
									uint32 bufferCount, size_t bufferSize);

			// Command
			void				DumpRing(xhci_trb *trb, uint32 size);
			void				QueueCommand(xhci_trb *trb);
			void				HandleCmdComplete(xhci_trb *trb);
			void				CompleteTransferDesc(XhciTransferDesc* td);
			void				HandleTransferComplete(xhci_trb *trb);
			status_t			DoCommand(xhci_trb *trb);

			// Doorbell
			void				Ring(uint8 slot, uint8 endpoint);

			// Commands
			status_t			Noop();
			status_t			EnableSlot(uint8 *slot);
			status_t			DisableSlot(uint8 slot);
			status_t			SetAddress(uint64 inputContext, bool bsr,
									uint8 slot);
			status_t			ConfigureEndpoint(uint64 inputContext,
									bool deconfigure, uint8 slot);
			status_t			EvaluateContext(uint64 inputContext,
									uint8 slot);
			status_t			ResetEndpoint(bool preserve, XhciEndpoint* endpoint);
			status_t			StopEndpoint(bool suspend, XhciEndpoint* endpoint);
			status_t			SetTRDequeue(uint64 dequeue, uint16 stream,
									uint8 endpoint, uint8 slot);
			status_t			ResetDevice(uint8 slot);

			// Operational register functions
	inline	void				WriteOpReg(uint32 reg, uint32 value);
	inline	uint32				ReadOpReg(uint32 reg);
	inline	status_t			WaitOpBits(uint32 reg, uint32 mask, uint32 expected);

			// Capability register functions
	inline	uint32				ReadCapReg32(uint32 reg);
	inline	void				WriteCapReg32(uint32 reg, uint32 value);

			// Runtime register functions
	inline	uint32				ReadRunReg32(uint32 reg);
	inline	void				WriteRunReg32(uint32 reg, uint32 value);

			// Doorbell register functions
	inline	uint32				ReadDoorReg32(uint32 reg);
	inline	void				WriteDoorReg32(uint32 reg, uint32 value);

			// Context functions
	inline	addr_t				_OffsetContextAddr(addr_t p);
	inline	uint32				_ReadContext(uint32* p);
	inline	void				_WriteContext(uint32* p, uint32 value);
	inline	uint64				_ReadContext(uint64* p);
	inline	void				_WriteContext(uint64* p, uint64 value);

			void				_SwitchIntelPorts();


private:
			friend class XhciDevice;
			friend class XhciEndpoint;
			friend class XhciRing;
			friend class XhciTransferDesc;

			DeviceNode*			fNode;
			UsbBusManager*		fBusManager {};

			area_id				fRegisterArea = -1;
			uint8*				fRegisters {};
			uint32				fCapabilityRegisterOffset {};
			uint32				fOperationalRegisterOffset {};
			uint32				fRuntimeRegisterOffset {};
			uint32				fDoorbellRegisterOffset {};

			pci_info			fPCIInfo {};
			PciDevice*			fPciDevice {};

			UsbStack*			fStack {};
			uint8				fIRQ {};
			bool				fUseMSI {};

			area_id				fErstArea = -1;
			xhci_erst_element*	fErst {};
			xhci_trb*			fEventRing {};
			XhciRing			fCmdRing;
			uint32				fCmdResult[2] {};

			area_id				fDcbaArea = -1;
			struct xhci_device_context_array* fDcba {};

			spinlock			fSpinlock {};

			ConditionVariable	fCmdCompCond;

			// Root Hubs
			XHCI2RootHub		fRootHub2;
			XHCI3RootHub		fRootHub3;

			// Port management
			uint8				fPortCount {};
			uint8				fSlotCount {};
			usb_speed			fPortSpeeds[XHCI_MAX_PORTS] {};
			uint8				fRootHubPorts[XHCI_MAX_PORTS] {};

			// Scratchpad
			uint32				fScratchpadCount {};
			area_id				fScratchpadArea[XHCI_MAX_SCRATCHPADS] {};
			void*				fScratchpad[XHCI_MAX_SCRATCHPADS] {};

			// Devices
			std::optional<XhciDevice>
								fDevices[XHCI_MAX_DEVICES] {};
			int32				fContextSizeShift {}; // 0/1 for 32/64 bytes

			// Transfers
			DPCQueue			fCallbackQueue;

			// Events
			mutex				fEventLock {};
			DPCQueue			fEventQueue;

			uint16				fEventIdx {};
			uint8				fEventCcs = 1;

			uint32				fExitLatMax {};


	class EventDPCCallback: public ::DPCCallback {
	public:
		XHCI& Base() {return ContainerOf(*this, &XHCI::fEventDpcCallback);}

		void DoDPC(DPCQueue* queue) final;
	} fEventDpcCallback;

	class BusManager: public BusDriver {
	public:
		BusManager(XHCI& base): fBase(base) {}

		void* QueryInterface(const char* name) final;

	private:
		XHCI& fBase;
	} fBusManagerDriver;
};


// #pragma mark - Register access

inline void
XHCI::WriteOpReg(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fOperationalRegisterOffset + reg) = value;
}


inline uint32
XHCI::ReadOpReg(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fOperationalRegisterOffset + reg);
}


inline status_t
XHCI::WaitOpBits(uint32 reg, uint32 mask, uint32 expected)
{
	int loops = 0;
	uint32 value = ReadOpReg(reg);
	while ((value & mask) != expected) {
		snooze(1000);
		value = ReadOpReg(reg);
		if (loops == 100) {
			TRACE("delay waiting on reg 0x%" B_PRIX32 " match 0x%" B_PRIX32
				" (0x%" B_PRIX32 ")\n",	reg, expected, mask);
		} else if (loops > 250) {
			TRACE_ERROR("timeout waiting on reg 0x%" B_PRIX32
				" match 0x%" B_PRIX32 " (0x%" B_PRIX32 ")\n", reg, expected,
				mask);
			return B_ERROR;
		}
		loops++;
	}
	return B_OK;
}


inline uint32
XHCI::ReadCapReg32(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fCapabilityRegisterOffset + reg);
}


inline void
XHCI::WriteCapReg32(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fCapabilityRegisterOffset + reg) = value;
}


inline uint32
XHCI::ReadRunReg32(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fRuntimeRegisterOffset + reg);
}


inline void
XHCI::WriteRunReg32(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fRuntimeRegisterOffset + reg) = value;
}


inline uint32
XHCI::ReadDoorReg32(uint32 reg)
{
	return *(volatile uint32 *)(fRegisters + fDoorbellRegisterOffset + reg);
}


inline void
XHCI::WriteDoorReg32(uint32 reg, uint32 value)
{
	*(volatile uint32 *)(fRegisters + fDoorbellRegisterOffset + reg) = value;
}


inline addr_t
XHCI::_OffsetContextAddr(addr_t p)
{
	if (fContextSizeShift == 1) {
		// each structure is page aligned, each pointer is 32 bits aligned
		uint32 offset = p & ((B_PAGE_SIZE - 1) & ~31U);
		p += offset;
	}
	return p;
}

inline uint32
XHCI::_ReadContext(uint32* p)
{
	p = (uint32*)_OffsetContextAddr((addr_t)p);
	return *p;
}


inline void
XHCI::_WriteContext(uint32* p, uint32 value)
{
	p = (uint32*)_OffsetContextAddr((addr_t)p);
	*p = value;
}


inline uint64
XHCI::_ReadContext(uint64* p)
{
	p = (uint64*)_OffsetContextAddr((addr_t)p);
	return *p;
}


inline void
XHCI::_WriteContext(uint64* p, uint64 value)
{
	p = (uint64*)_OffsetContextAddr((addr_t)p);
	*p = value;
}
