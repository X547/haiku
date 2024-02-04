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


static status_t
usb_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT: {
			new((void*)&Stack::Instance()) Stack();
			DetachableScopeExit stackDeleter([] {
				Stack::Instance().~Stack();
			});

			CHECK_RET(Stack::Instance().InitCheck());

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


extern driver_module_info gUsbHubDriverModule;

static driver_module_info sUsbDriverModule = {
	.info = {
		.name = USB_DRIVER_MODULE_NAME,
		.std_ops = usb_std_ops,
	},
	.probe = UsbBusManagerImpl::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sUsbDriverModule,
	(module_info* )&gUsbHubDriverModule,
	NULL
};
