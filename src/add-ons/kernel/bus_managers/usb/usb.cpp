#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/PCI.h>
#include <dm2/bus/USB.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <ScopeExit.h>

#include "usb_private.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define USB_DRIVER_MODULE_NAME "bus_managers/usb/driver/v1"


class UsnBusManager: public DeviceDriver {
public:
	UsnBusManager(DeviceNode* node): fNode(node) {}
	virtual ~UsnBusManager();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	DeviceNode* fNode;
	PciDevice* fPciDevice {};
};


UsnBusManager::~UsnBusManager()
{
}


status_t
UsnBusManager::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<UsnBusManager> driver(new(std::nothrow) UsnBusManager(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
UsnBusManager::Init()
{
	fPciDevice = fNode->QueryBusInterface<PciDevice>();

	// TODO: implement

	return B_OK;
}


// #pragma mark -

static status_t
usb_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT: {
			new((void*)&Stack::Instance()) Stack();
			DetachableScopeExit stackDeleter([] {
				Stack::Instance().~Stack();
			});

			CHECK_RET(Stack::Instance().Init());

			stackDeleter.Detach();
			return B_OK;
		}
		case B_MODULE_UNINIT: {
			Stack::Instance().~Stack();
			return B_OK;
		}

		default:
			return B_ERROR;
	}
}


static driver_module_info sUsbDriverModule = {
	.info = {
		.name = USB_DRIVER_MODULE_NAME,
		.std_ops = usb_std_ops,
	},
	.probe = UsnBusManager::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sUsbDriverModule,
	NULL
};
