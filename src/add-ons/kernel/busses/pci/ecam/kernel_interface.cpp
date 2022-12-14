/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "PciControllerEcam.h"


device_manager_info* gDeviceManager;


pci_controller_module_info gPciControllerDriver = {
	.info = {
		.info = {
			.name = ECAM_PCI_DRIVER_MODULE_NAME,
		},
		.supports_device = [](device_node* parent) {
			return PciControllerEcam::SupportsDevice(parent);
		},
		.register_device = [](device_node* parent) {
			return PciControllerEcam::RegisterDevice(parent);
		},
		.init_driver = [](device_node* node, void** driverCookie) {
			return PciControllerEcam::InitDriver(node, *(PciControllerEcam**)driverCookie);
		},
		.uninit_driver = [](void* driverCookie) {
			return static_cast<PciControllerEcam*>(driverCookie)->UninitDriver();
		},
	},
	.read_pci_config = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32* value) {
		return static_cast<PciControllerEcam*>(cookie)
			->ReadConfig(bus, device, function, offset, size, *value);
	},
	.write_pci_config = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) {
		return static_cast<PciControllerEcam*>(cookie)
			->WriteConfig(bus, device, function, offset, size, value);
	},
	.get_max_bus_devices = [](void* cookie, int32* count) {
		return static_cast<PciControllerEcam*>(cookie)->GetMaxBusDevices(*count);
	},
	.read_pci_irq = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 *irq) {
		return static_cast<PciControllerEcam*>(cookie)->ReadIrq(bus, device, function, pin, *irq);
	},
	.write_pci_irq = [](void* cookie,
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 irq) {
		return static_cast<PciControllerEcam*>(cookie)->WriteIrq(bus, device, function, pin, irq);
	}
};


_EXPORT module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&gPciControllerDriver,
	NULL
};
