#pragma once


#include <dm2/bus/USB.h>
#include <dm2/bus/PCI.h>

#include <lock.h>
#include <util/iovec_support.h>

#include "usbspec_private.h"
#include "xhci_hardware.h"


/*
#define TRACE_OUTPUT(x, y, z...) \
	{ \
		dprintf("usb %s%s %" B_PRId32 ": ", y, (x)->TypeName(), (x)->USBID()); \
		dprintf(z); \
	}
*/

#define TRACE_OUTPUT(x, y, z...) dprintf(z)

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


struct xhci_td;
struct xhci_device;
struct xhci_endpoint;
class XHCIRootHub;


/* Each transfer requires 2 TRBs on the endpoint "ring" (one for the link TRB,
 * and one for the Event Data TRB), plus one more at the end for the link TRB
 * to the start. */
#define XHCI_ENDPOINT_RING_SIZE	(XHCI_MAX_TRANSFERS * 2 + 1)


typedef struct xhci_td {
	xhci_trb*	trbs;
	phys_addr_t	trb_addr;
	uint32		trb_count;
	uint32		trb_used;

	void**		buffers;
	phys_addr_t* buffer_addrs;
	size_t		buffer_size;
	uint32		buffer_count;

	UsbBusTransfer*	transfer;
	uint8		trb_completion_code;
	int32		td_transferred;
	int32		trb_left;

	xhci_td*	next;
} xhci_td;


typedef struct xhci_endpoint {
	mutex 			lock;

	xhci_device*	device;
	uint8			id;

	uint16			max_burst_payload;

	xhci_td*		td_head;
	uint8			used;
	uint8			current;

	xhci_trb*		trbs; // [XHCI_ENDPOINT_RING_SIZE]
	phys_addr_t 	trb_addr;
} xhci_endpoint;


typedef struct xhci_device {
	uint8 slot;
	uint8 address;
	area_id trb_area;
	phys_addr_t trb_addr;
	struct xhci_trb *trbs; // [XHCI_MAX_ENDPOINTS - 1][XHCI_ENDPOINT_RING_SIZE]

	area_id input_ctx_area;
	phys_addr_t input_ctx_addr;
	struct xhci_input_device_ctx *input_ctx;

	area_id device_ctx_area;
	phys_addr_t device_ctx_addr;
	struct xhci_device_ctx *device_ctx;

	xhci_endpoint endpoints[XHCI_MAX_ENDPOINTS - 1];
} xhci_device;


class XHCI: public DeviceDriver, public UsbHostController {
public:
					XHCI(DeviceNode* node): fNode(node), fBusManagerDriver(*this) {}
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
			status_t			GetPortStatus(uint8 index,
									usb_port_status *status);
			status_t			SetPortFeature(uint8 index, uint16 feature);
			status_t			ClearPortFeature(uint8 index, uint16 feature);

			status_t			GetPortSpeed(uint8 index, usb_speed *speed);

private:
	inline	bool				Lock() { return fBusManager->Lock(); }
	inline	void				Unlock() { return fBusManager->Unlock(); }

			status_t			Init();

			// Controller resets
			status_t			ControllerReset();
			status_t			ControllerHalt();

			// Interrupt functions
	static	int32				InterruptHandler(void *data);
			int32				Interrupt();

			// Device management
			void				CleanupDevice(xhci_device *device);

			// Endpoint management
			status_t			ConfigureEndpoint(xhci_endpoint* ep, uint8 slot,
									uint8 number, uint8 type, bool directionIn,
									uint16 interval, uint16 maxPacketSize,
									usb_speed speed, uint8 maxBurst,
									uint16 bytesPerInterval);
			uint8				_GetEndpointState(xhci_endpoint* ep);

			status_t			_InsertEndpointForPipe(UsbBusPipe *pipe);
			status_t			_RemoveEndpointForPipe(UsbBusPipe *pipe);

			// Event management
	static	int32				EventThread(void *data);
			void				CompleteEvents();
			void				ProcessEvents();

			// Transfer management
	static	int32				FinishThread(void *data);
			void				FinishTransfers();

			// Descriptor management
			xhci_td *			CreateDescriptor(uint32 trbCount,
									uint32 bufferCount, size_t bufferSize);
			void				FreeDescriptor(xhci_td *descriptor);

			size_t				WriteDescriptor(xhci_td *descriptor,
									generic_io_vec *vector, size_t vectorCount, bool physical);
			size_t				ReadDescriptor(xhci_td *descriptor,
									generic_io_vec *vector, size_t vectorCount, bool physical);

			status_t			_LinkDescriptorForPipe(xhci_td *descriptor,
									xhci_endpoint *endpoint);
			status_t			_UnlinkDescriptorForPipe(xhci_td *descriptor,
									xhci_endpoint *endpoint);

			// Command
			void				DumpRing(xhci_trb *trb, uint32 size);
			void				QueueCommand(xhci_trb *trb);
			void				HandleCmdComplete(xhci_trb *trb);
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
			status_t			ResetEndpoint(bool preserve, xhci_endpoint* endpoint);
			status_t			StopEndpoint(bool suspend, xhci_endpoint* endpoint);
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
			DeviceNode*			fNode;
			UsbBusManager*		fBusManager {};

			area_id				fRegisterArea = -1;
			uint8*				fRegisters {};
			uint32				fCapabilityRegisterOffset {};
			uint32				fOperationalRegisterOffset {};
			uint32				fRuntimeRegisterOffset {};
			uint32				fDoorbellRegisterOffset {};

			pci_info			fPCIInfo {};
			PciDevice*			fDevice {};

			UsbStack*			fStack {};
			uint8				fIRQ {};
			bool				fUseMSI {};

			area_id				fErstArea = -1;
			xhci_erst_element*	fErst {};
			xhci_trb*			fEventRing {};
			xhci_trb*			fCmdRing {};
			uint64				fCmdAddr {};
			uint32				fCmdResult[2] {};

			area_id				fDcbaArea = -1;
			struct xhci_device_context_array* fDcba {};

			spinlock			fSpinlock {};

			sem_id				fCmdCompSem = -1;
			bool				fStopThreads {};

			// Root Hub
			UsbBusDevice*		fRootHub {};

			// Port management
			uint8				fPortCount {};
			uint8				fSlotCount {};
			usb_speed			fPortSpeeds[XHCI_MAX_PORTS] {};

			// Scratchpad
			uint32				fScratchpadCount {};
			area_id				fScratchpadArea[XHCI_MAX_SCRATCHPADS] {};
			void*				fScratchpad[XHCI_MAX_SCRATCHPADS] {};

			// Devices
			struct xhci_device	fDevices[XHCI_MAX_DEVICES] {};
			int32				fContextSizeShift {}; // 0/1 for 32/64 bytes

			// Transfers
			mutex				fFinishedLock {};
			xhci_td*			fFinishedHead {};
			sem_id				fFinishTransfersSem = -1;
			thread_id			fFinishThread = -1;

			// Events
			sem_id				fEventSem = -1;
			thread_id			fEventThread = -1;
			mutex				fEventLock {};
			uint16				fEventIdx {};
			uint16				fCmdIdx {};
			uint8				fEventCcs = 1;
			uint8				fCmdCcs = 1;

			uint32				fExitLatMax {};


	class BusManager: public BusDriver {
	public:
		BusManager(XHCI& base): fBase(base) {}

		void* QueryInterface(const char* name) final;

	private:
		XHCI& fBase;
	} fBusManagerDriver;
};


class XHCIRootHub {
public:
static	status_t					Create(UsbBusDevice*& outHub, UsbBusManager *busManager,
										int8 deviceAddress);

static	status_t					ProcessTransfer(XHCI *xhci, UsbBusTransfer *transfer);
};
