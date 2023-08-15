#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/PCI.h>
#include <dm2/bus/USB.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define XHCI_DRIVER_MODULE_NAME "busses/usb/xhci/driver/v1"


class XHCI: public DeviceDriver, public UsbHostController {
public:
	XHCI(DeviceNode* node): fNode(node), fBusManagerDriver(*this) {}
	virtual ~XHCI();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	// UsbHostController
	void			SetBusManager(UsbBusManager* busManager) final;

	UsbBusDevice*	AllocateDevice(UsbBusHub* parent,
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

private:
	status_t Init();

private:
	DeviceNode* fNode;
	PciDevice* fPciDevice {};

	UsbBusManager* fBusManager {};

	class BusManager: public BusDriver {
	public:
		BusManager(XHCI& base): fBase(base) {}

		void* QueryInterface(const char* name) final;

	private:
		XHCI& fBase;
	} fBusManagerDriver;
};


XHCI::~XHCI()
{
}


status_t
XHCI::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<XHCI> driver(new(std::nothrow) XHCI(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
XHCI::Init()
{
	fPciDevice = fNode->QueryBusInterface<PciDevice>();

	static const device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "USB Bus Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/usb/driver/v1"}},
		{}
	};
	CHECK_RET(fNode->RegisterNode(this, static_cast<BusDriver*>(&fBusManagerDriver), attrs, NULL));

	return B_OK;
}


void
XHCI::SetBusManager(UsbBusManager* busManager)
{
	fBusManager = busManager;
}


UsbBusDevice*
XHCI::AllocateDevice(UsbBusHub* parent,
	int8 hubAddress, uint8 hubPort,
	usb_speed speed)
{
	return NULL;
}


void
XHCI::FreeDevice(UsbBusDevice* device)
{
}


status_t
XHCI::Start()
{
	return ENOSYS;
}


status_t
XHCI::Stop()
{
	return ENOSYS;
}


status_t
XHCI::StartDebugTransfer(UsbBusTransfer* transfer)
{
	return ENOSYS;
}


status_t
XHCI::CheckDebugTransfer(UsbBusTransfer* transfer)
{
	return ENOSYS;
}


void
XHCI::CancelDebugTransfer(UsbBusTransfer* transfer)
{
}


status_t
XHCI::SubmitTransfer(UsbBusTransfer* transfer)
{
	return ENOSYS;
}


status_t
XHCI::CancelQueuedTransfers(UsbBusPipe* pipe, bool force)
{
	return ENOSYS;
}


status_t
XHCI::NotifyPipeChange(UsbBusPipe* pipe, usb_change change)
{
	return ENOSYS;
}


void*
XHCI::BusManager::QueryInterface(const char* name)
{
	if (strcmp(name, PciController::ifaceName) == 0)
		return static_cast<UsbHostController*>(&fBase);

	return NULL;
}


static driver_module_info sXhciDriverModule = {
	.info = {
		.name = XHCI_DRIVER_MODULE_NAME,
	},
	.probe = XHCI::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sXhciDriverModule,
	NULL
};
