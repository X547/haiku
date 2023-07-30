/*
 * Copyright 2020, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _AUTO_DELETER_DRIVERS_H
#define _AUTO_DELETER_DRIVERS_H


#include <AutoDeleter.h>
#include <driver_settings.h>

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)
#include <vfs.h>
#include <fs/fd.h>
#include <vm/VMAddressSpace.h>
#endif


namespace BPrivate {


typedef CObjectDeleter<void, status_t, unload_driver_settings>
	DriverSettingsUnloader;

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)

typedef CObjectDeleter<struct vnode, void, vfs_put_vnode> VnodePutter;
typedef CObjectDeleter<file_descriptor, void, put_fd> DescriptorPutter;
typedef MethodDeleter<VMAddressSpace, void, &VMAddressSpace::Put>
	VMAddressSpacePutter;

#endif


}


using ::BPrivate::DriverSettingsUnloader;

#if defined(_KERNEL_MODE) && !defined(_BOOT_MODE)

using ::BPrivate::VnodePutter;
using ::BPrivate::DescriptorPutter;
using ::BPrivate::VMAddressSpacePutter;

#endif


#endif	// _AUTO_DELETER_DRIVERS_H
