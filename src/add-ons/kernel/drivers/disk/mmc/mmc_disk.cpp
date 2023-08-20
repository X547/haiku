#include "mmc_disk.h"

#include <stdio.h>
#include <new>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <fs/devfs.h>


#define VIRTIO_BLOCK_DRIVER_MODULE_NAME "drivers/disk/mmc_disk/driver/v1"


// #pragma mark - MmcDiskDriver

status_t
MmcDiskDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<MmcDiskDriver> driver(new(std::nothrow) MmcDiskDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t MmcDiskDriver::Init()
{
	CALLED();

	fDmaResource.SetTo(new(std::nothrow) DMAResource);
	if (!fDmaResource.IsSet())
		return B_NO_MEMORY;

	fMmcDevice = fNode->QueryBusInterface<MmcDevice>();

	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), "disk/mmc/%" B_PRId32 "/raw", id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


status_t
MmcDiskDriver::DoIO(IOOperation* operation)
{
	return ENOSYS;
}


status_t
MmcDiskDriver::GetGeometry(device_geometry* geometry)
{
	return ENOSYS;
}


// #pragma mark - MmcDiskDevFsNode

status_t
MmcDiskDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	CALLED();

	ObjectDeleter<MmcDiskDevFsNodeHandle> handle(new(std::nothrow) MmcDiskDevFsNodeHandle(fDriver));
	if (!handle.IsSet())
		return B_NO_MEMORY;

	*outHandle = handle.Detach();
	return B_OK;
}


// #pragma mark - MmcDiskDevFsNodeHandle

status_t
MmcDiskDevFsNodeHandle::IO(io_request *request)
{
	return fDriver.fIoScheduler->ScheduleRequest(request);
}


status_t
MmcDiskDevFsNodeHandle::Control(uint32 op, void *buffer, size_t length)
{
	CALLED();

	TRACE("ioctl(op = %" B_PRIu32 ")\n", op);

	switch (op) {
		default:
			break;
	}

	return B_DEV_INVALID_IOCTL;
}


static driver_module_info sMmcDiskDriver = {
	.info = {
		.name = VIRTIO_BLOCK_DRIVER_MODULE_NAME,
	},
	.probe = MmcDiskDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sMmcDiskDriver,
	NULL
};
