/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "ps2_input.h"


device_manager_info *gDeviceManager;


_EXPORT module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&gDeviceManager },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&gControllerModuleInfo,
	(module_info *)&gMouseModuleInfo,
	(module_info *)&gMouseDeviceModuleInfo,
	NULL
};
