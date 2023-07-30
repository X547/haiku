/*
 * Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include <kdevice_manager.h>

#include <KernelExport.h>

#include "IOSchedulerRoster.h"


//#define TRACE_DEVICE_MANAGER
#ifdef TRACE_DEVICE_MANAGER
#	define TRACE(a) dprintf a
#else
#	define TRACE(a) ;
#endif


static device_manager_info* sDeviceManager;


status_t
device_manager_init(struct kernel_args* args)
{
	IOSchedulerRoster::Init();

	if (get_module(B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&sDeviceManager) < B_OK) {
		panic("can't load device manager module");
	}

	return B_OK;
}


status_t
device_manager_init_post_modules(struct kernel_args* args)
{
	return sDeviceManager->file_system_mounted();
}
