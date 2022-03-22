/*
 * Copyright 2003-2006, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "bios.h"
#include "virtio.h"
#include "VirtioBlockDevice.h"
#include "AtaBlockDevice.h"
#include "NvmeBlockDevice.h"

#include <KernelExport.h>
#include <boot/platform.h>
#include <boot/partitions.h>
#include <boot/stdio.h>
#include <boot/stage2.h>

#include <AutoDeleter.h>

#include <string.h>
#include <new>
#include <algorithm>

//#define TRACE_DEVICES
#ifdef TRACE_DEVICES
# define TRACE(x...) dprintf(x)
#else
# define TRACE(x...) ;
#endif


static off_t
get_next_check_sum_offset(int32 index, off_t maxSize)
{
	if (index < 2)
		return index * 512;

	if (index < 4)
		return (maxSize >> 10) + index * 2048;

	return ((system_time() + index) % (maxSize >> 9)) * 512;
}


static uint32
compute_check_sum(Node* device, off_t offset)
{
	char buffer[512];
	ssize_t bytesRead = device->ReadAt(NULL, offset, buffer, sizeof(buffer));
	if (bytesRead < B_OK)
		return 0;

	if (bytesRead < (ssize_t)sizeof(buffer))
		memset(buffer + bytesRead, 0, sizeof(buffer) - bytesRead);

	uint32* array = (uint32*)buffer;
	uint32 sum = 0;

	for (uint32 i = 0; i < (bytesRead + sizeof(uint32) - 1) / sizeof(uint32);
		i++)
		sum += array[i];

	return sum;
}


//#pragma mark -

status_t
platform_add_boot_device(struct stage2_args* args, NodeList* devicesList)
{
	ObjectDeleter<Node> device(CreateAtaBlockDev());
	if (device.IsSet()) {
		devicesList->Insert(device.Detach());
	}
	return devicesList->Count() > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}


status_t
platform_add_block_devices(struct stage2_args* args, NodeList* devicesList)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
platform_get_boot_partitions(struct stage2_args* args, Node* bootDevice,
	NodeList *list, NodeList *partitionList)
{
	NodeIterator iterator = list->GetIterator();
	boot::Partition *partition = NULL;
	while ((partition = (boot::Partition *)iterator.Next()) != NULL) {
		// ToDo: just take the first partition for now
		partitionList->Insert(partition);
		return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}


status_t
platform_register_boot_device(Node* device)
{
	TRACE("%s: called\n", __func__);

	disk_identifier identifier;

	identifier.bus_type = UNKNOWN_BUS;
	identifier.device_type = UNKNOWN_DEVICE;
	identifier.device.unknown.size = device->Size();

	for (uint32 i = 0; i < NUM_DISK_CHECK_SUMS; ++i) {
		off_t offset = get_next_check_sum_offset(i, device->Size());
		identifier.device.unknown.check_sums[i].offset = offset;
		identifier.device.unknown.check_sums[i].sum = compute_check_sum(device,
			offset);
	}

	gBootVolume.SetInt32(BOOT_METHOD, BOOT_METHOD_HARD_DISK);
	gBootVolume.SetBool(BOOT_VOLUME_BOOTED_FROM_IMAGE, false);
	gBootVolume.SetData(BOOT_VOLUME_DISK_IDENTIFIER, B_RAW_TYPE,
		&identifier, sizeof(disk_identifier));

	return B_OK;
}


void
platform_cleanup_devices()
{
}
