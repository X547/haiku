/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "ECAMPCIController.h"


device_manager_info* gDeviceManager;
pci_module_info* gPCI;


static driver_module_info sPciControllerDriver = {
	.info = {
		.name = ECAM_PCI_DRIVER_MODULE_NAME,
	},
	.probe = ECAMPCIController::Probe
};


_EXPORT module_dependency module_dependencies[] = {
	{ B_PCI_MODULE_NAME, (module_info**)&gPCI },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&sPciControllerDriver,
	NULL
};
