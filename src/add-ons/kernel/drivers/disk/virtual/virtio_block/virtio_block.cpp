/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Distributed under the terms of the MIT License.
 */


#include "virtio_block.h"

#include <stdio.h>
#include <new>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <fs/devfs.h>


static const uint8 kDriveIcon[] = {
	0x6e, 0x63, 0x69, 0x66, 0x08, 0x03, 0x01, 0x00, 0x00, 0x02, 0x00, 0x16,
	0x02, 0x3c, 0xc7, 0xee, 0x38, 0x9b, 0xc0, 0xba, 0x16, 0x57, 0x3e, 0x39,
	0xb0, 0x49, 0x77, 0xc8, 0x42, 0xad, 0xc7, 0x00, 0xff, 0xff, 0xd3, 0x02,
	0x00, 0x06, 0x02, 0x3c, 0x96, 0x32, 0x3a, 0x4d, 0x3f, 0xba, 0xfc, 0x01,
	0x3d, 0x5a, 0x97, 0x4b, 0x57, 0xa5, 0x49, 0x84, 0x4d, 0x00, 0x47, 0x47,
	0x47, 0xff, 0xa5, 0xa0, 0xa0, 0x02, 0x00, 0x16, 0x02, 0xbc, 0x59, 0x2f,
	0xbb, 0x29, 0xa7, 0x3c, 0x0c, 0xe4, 0xbd, 0x0b, 0x7c, 0x48, 0x92, 0xc0,
	0x4b, 0x79, 0x66, 0x00, 0x7d, 0xff, 0xd4, 0x02, 0x00, 0x06, 0x02, 0x38,
	0xdb, 0xb4, 0x39, 0x97, 0x33, 0xbc, 0x4a, 0x33, 0x3b, 0xa5, 0x42, 0x48,
	0x6e, 0x66, 0x49, 0xee, 0x7b, 0x00, 0x59, 0x67, 0x56, 0xff, 0xeb, 0xb2,
	0xb2, 0x03, 0xa7, 0xff, 0x00, 0x03, 0xff, 0x00, 0x00, 0x04, 0x01, 0x80,
	0x07, 0x0a, 0x06, 0x22, 0x3c, 0x22, 0x49, 0x44, 0x5b, 0x5a, 0x3e, 0x5a,
	0x31, 0x39, 0x25, 0x0a, 0x04, 0x22, 0x3c, 0x44, 0x4b, 0x5a, 0x31, 0x39,
	0x25, 0x0a, 0x04, 0x44, 0x4b, 0x44, 0x5b, 0x5a, 0x3e, 0x5a, 0x31, 0x0a,
	0x04, 0x22, 0x3c, 0x22, 0x49, 0x44, 0x5b, 0x44, 0x4b, 0x08, 0x02, 0x27,
	0x43, 0xb8, 0x14, 0xc1, 0xf1, 0x08, 0x02, 0x26, 0x43, 0x29, 0x44, 0x0a,
	0x05, 0x44, 0x5d, 0x49, 0x5d, 0x60, 0x3e, 0x5a, 0x3b, 0x5b, 0x3f, 0x08,
	0x0a, 0x07, 0x01, 0x06, 0x00, 0x0a, 0x00, 0x01, 0x00, 0x10, 0x01, 0x17,
	0x84, 0x00, 0x04, 0x0a, 0x01, 0x01, 0x01, 0x00, 0x0a, 0x02, 0x01, 0x02,
	0x00, 0x0a, 0x03, 0x01, 0x03, 0x00, 0x0a, 0x04, 0x01, 0x04, 0x10, 0x01,
	0x17, 0x85, 0x20, 0x04, 0x0a, 0x06, 0x01, 0x05, 0x30, 0x24, 0xb3, 0x99,
	0x01, 0x17, 0x82, 0x00, 0x04, 0x0a, 0x05, 0x01, 0x05, 0x30, 0x20, 0xb2,
	0xe6, 0x01, 0x17, 0x82, 0x00, 0x04
};


#define VIRTIO_BLOCK_DRIVER_MODULE_NAME "drivers/disk/virtual/virtio_block/driver/v1"


const char *
get_feature_name(uint32 feature)
{
	switch (feature) {
		case VIRTIO_BLK_F_BARRIER:
			return "host barrier";
		case VIRTIO_BLK_F_SIZE_MAX:
			return "maximum segment size";
		case VIRTIO_BLK_F_SEG_MAX:
			return "maximum segment count";
		case VIRTIO_BLK_F_GEOMETRY:
			return "disk geometry";
		case VIRTIO_BLK_F_RO:
			return "read only";
		case VIRTIO_BLK_F_BLK_SIZE:
			return "block size";
		case VIRTIO_BLK_F_SCSI:
			return "scsi commands";
		case VIRTIO_BLK_F_FLUSH:
			return "flush command";
		case VIRTIO_BLK_F_TOPOLOGY:
			return "topology";
		case VIRTIO_BLK_F_CONFIG_WCE:
			return "config wce";
	}
	return NULL;
}


// #pragma mark - VirtioBlockDriver

status_t
VirtioBlockDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<VirtioBlockDriver> driver(new(std::nothrow) VirtioBlockDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t VirtioBlockDriver::Init()
{
	CALLED();

	fMediaStatus = B_OK;
	fDmaResource.SetTo(new(std::nothrow) DMAResource);
	if (!fDmaResource.IsSet())
		return B_NO_MEMORY;

	fSemCb.SetTo(create_sem(0, "virtio_block_cb"));

	DeviceNodePutter parent(fNode->GetParent());
	fVirtioDevice = parent->QueryBusInterface<VirtioDevice>();

	fVirtioDevice->NegotiateFeatures(
		VIRTIO_BLK_F_BARRIER | VIRTIO_BLK_F_SIZE_MAX
			| VIRTIO_BLK_F_SEG_MAX | VIRTIO_BLK_F_GEOMETRY
			| VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE
			| VIRTIO_BLK_F_FLUSH | VIRTIO_BLK_F_TOPOLOGY
			| VIRTIO_FEATURE_RING_INDIRECT_DESC,
		&fFeatures, &get_feature_name);

	status_t status = fVirtioDevice->ReadDeviceConfig(0, &fConfig, sizeof(struct virtio_blk_config));
	if (status != B_OK)
		return status;

	SetCapacity();

	TRACE("virtio_block: capacity: %" B_PRIu64 ", block_size %" B_PRIu32 "\n",
		fCapacity, fBlockSize);

	status = fVirtioDevice->AllocQueues(1, &fVirtioQueue);
	if (status != B_OK) {
		ERROR("queue allocation failed (%s)\n", strerror(status));
		return status;
	}
	status = fVirtioDevice->SetupInterrupt(ConfigCallback, this);

	if (status == B_OK)
		status = fVirtioQueue->SetupInterrupt(Callback, this);

	return status;
}


void
VirtioBlockDriver::Free()
{
	delete this;
}


status_t
VirtioBlockDriver::RegisterChildDevices()
{
	CALLED();

	static int32 id = 0;
	id++;

	char name[64];
	snprintf(name, sizeof(name), "disk/virtual/virtio_block/%" B_PRId32 "/raw", id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


status_t
VirtioBlockDriver::DoIO(IOOperation* operation)
{
	size_t bytesTransferred = 0;
	status_t status = B_OK;

	physical_entry entries[operation->VecCount() + 2];

	void *buffer = malloc(sizeof(struct virtio_blk_outhdr) + sizeof(uint8));
	struct virtio_blk_outhdr *header = (struct virtio_blk_outhdr*)buffer;
	header->type = operation->IsWrite() ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
	header->sector = operation->Offset() / 512;
	header->ioprio = 1;

	uint8* ack = (uint8*)buffer + sizeof(struct virtio_blk_outhdr);
	*ack = 0xff;

	get_memory_map(buffer, sizeof(struct virtio_blk_outhdr) + sizeof(uint8),
		&entries[0], 1);
	entries[operation->VecCount() + 1].address = entries[0].address
		+ sizeof(struct virtio_blk_outhdr);
	entries[operation->VecCount() + 1].size = sizeof(uint8);
	entries[0].size = sizeof(struct virtio_blk_outhdr);

	memcpy(entries + 1, operation->Vecs(), operation->VecCount()
		* sizeof(physical_entry));

	fVirtioQueue->RequestV(entries,
		1 + (operation->IsWrite() ? operation->VecCount() : 0 ),
		1 + (operation->IsWrite() ? 0 : operation->VecCount()),
		this);

	acquire_sem(fSemCb.Get());

	switch (*ack) {
		case VIRTIO_BLK_S_OK:
			status = B_OK;
			bytesTransferred = operation->Length();
			break;
		case VIRTIO_BLK_S_UNSUPP:
			status = ENOTSUP;
			break;
		default:
			status = EIO;
			break;
	}
	free(buffer);

	fIoScheduler->OperationCompleted(operation, status, bytesTransferred);
	return status;
}


status_t
VirtioBlockDriver::GetGeometry(device_geometry* geometry)
{
	devfs_compute_geometry_size(geometry, fCapacity, fBlockSize);
	geometry->bytes_per_physical_sector = fPhysicalBlockSize;

	geometry->device_type = B_DISK;
	geometry->removable = false;

	geometry->read_only = ((fFeatures & VIRTIO_BLK_F_RO) != 0);
	geometry->write_once = false;

	TRACE("virtio_block: GetGeometry(): %" B_PRIu32 ", %" B_PRIu32 ", %" B_PRIu32 ", %" B_PRIu32
		", %d, %d, %d, %d\n", geometry->bytes_per_sector, geometry->sectors_per_track,
		geometry->cylinder_count, geometry->head_count, geometry->device_type,
		geometry->removable, geometry->read_only, geometry->write_once);

	return B_OK;
}


bool
VirtioBlockDriver::SetCapacity()
{
	// get capacity
	uint32 blockSize = 512;
	if ((fFeatures & VIRTIO_BLK_F_BLK_SIZE) != 0)
		blockSize = fConfig.blk_size;
	uint64 capacity = fConfig.capacity * 512 / blockSize;
	uint32 physicalBlockSize = blockSize;

	if ((fFeatures & VIRTIO_BLK_F_TOPOLOGY) != 0
		&& fConfig.topology.physical_block_exp > 0) {
		physicalBlockSize = blockSize * (1 << fConfig.topology.physical_block_exp);
	}

	TRACE("set_capacity(device = %p, capacity = %" B_PRIu64 ", blockSize = %" B_PRIu32 ")\n",
		info, capacity, blockSize);

	if (fBlockSize == blockSize && fCapacity == capacity)
		return false;

	fCapacity = capacity;

	if (fBlockSize != 0) {
		ERROR("old %" B_PRId32 ", new %" B_PRId32 "\n", fBlockSize,
			blockSize);
		panic("updating DMAResource not yet implemented...");
	}

	dma_restrictions restrictions;
	memset(&restrictions, 0, sizeof(restrictions));
	if ((fFeatures & VIRTIO_BLK_F_SIZE_MAX) != 0)
		restrictions.max_segment_size = fConfig.size_max;
	if ((fFeatures & VIRTIO_BLK_F_SEG_MAX) != 0)
		restrictions.max_segment_count = fConfig.seg_max;

	// TODO: we need to replace the DMAResource in our IOScheduler
	status_t status = fDmaResource->Init(restrictions, blockSize, 1024, 32);
	if (status != B_OK)
		panic("initializing DMAResource failed: %s", strerror(status));

	fIoScheduler.SetTo(new(std::nothrow) IOSchedulerSimple(fDmaResource.Get()));
	if (!fIoScheduler.IsSet())
		panic("allocating IOScheduler failed.");

	// TODO: use whole device name here
	status = fIoScheduler->Init("virtio");
	if (status != B_OK)
		panic("initializing IOScheduler failed: %s", strerror(status));

	fIoScheduler->SetCallback(*static_cast<IOCallback*>(this));

	fBlockSize = blockSize;
	fPhysicalBlockSize = physicalBlockSize;
	return true;
}


void
VirtioBlockDriver::ConfigCallback(void* driverCookie)
{
	VirtioBlockDriver* driver = (VirtioBlockDriver*)driverCookie;

	status_t status = driver->fVirtioDevice->ReadDeviceConfig(0,
		&driver->fConfig, sizeof(struct virtio_blk_config));
	if (status != B_OK)
		return;

	if (driver->SetCapacity())
		driver->fMediaStatus = B_DEV_MEDIA_CHANGED;
}


void
VirtioBlockDriver::Callback(void* driverCookie, void* cookie)
{
	VirtioBlockDriver* driver = (VirtioBlockDriver*)cookie;

	// consume all queued elements
	while (driver->fVirtioQueue->Dequeue(NULL, NULL))
		;

	release_sem_etc(driver->fSemCb.Get(), 1, B_DO_NOT_RESCHEDULE);
}


// #pragma mark - VirtioBlockDevFsNode

status_t
VirtioBlockDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	CALLED();

	ObjectDeleter<VirtioBlockDevFsNodeHandle> handle(new(std::nothrow) VirtioBlockDevFsNodeHandle(fDriver));
	if (!handle.IsSet())
		return B_NO_MEMORY;

	*outHandle = handle.Detach();
	return B_OK;
}


// #pragma mark - VirtioBlockDevFsNodeHandle

void
VirtioBlockDevFsNodeHandle::Free()
{
	delete this;
}


status_t
VirtioBlockDevFsNodeHandle::IO(io_request *request)
{
	return fDriver.fIoScheduler->ScheduleRequest(request);
}


status_t
VirtioBlockDevFsNodeHandle::Control(uint32 op, void *buffer, size_t length)
{
	CALLED();

	TRACE("ioctl(op = %" B_PRIu32 ")\n", op);

	switch (op) {
		case B_GET_MEDIA_STATUS:
		{
			*(status_t *)buffer = fDriver.fMediaStatus;
			fDriver.fMediaStatus = B_OK;
			TRACE("B_GET_MEDIA_STATUS: 0x%08" B_PRIx32 "\n", *(status_t *)buffer);
			return B_OK;
			break;
		}

		case B_GET_DEVICE_SIZE:
		{
			size_t size = fDriver.fCapacity * fDriver.fBlockSize;
			return user_memcpy(buffer, &size, sizeof(size_t));
		}

		case B_GET_GEOMETRY:
		{
			if (buffer == NULL || length > sizeof(device_geometry))
				return B_BAD_VALUE;

		 	device_geometry geometry;
			status_t status = fDriver.GetGeometry(&geometry);
			if (status != B_OK)
				return status;

			return user_memcpy(buffer, &geometry, length);
		}

		case B_GET_ICON_NAME:
			return user_strlcpy((char*)buffer, "devices/drive-harddisk", B_FILE_NAME_LENGTH);

		case B_GET_VECTOR_ICON:
		{
			// TODO: take device type into account!
			device_icon iconData;
			if (length != sizeof(device_icon))
				return B_BAD_VALUE;
			if (user_memcpy(&iconData, buffer, sizeof(device_icon)) != B_OK)
				return B_BAD_ADDRESS;

			if (iconData.icon_size >= (int32)sizeof(kDriveIcon)) {
				if (user_memcpy(iconData.icon_data, kDriveIcon, sizeof(kDriveIcon)) != B_OK)
					return B_BAD_ADDRESS;
			}

			iconData.icon_size = sizeof(kDriveIcon);
			return user_memcpy(buffer, &iconData, sizeof(device_icon));
		}

		/*case B_FLUSH_DRIVE_CACHE:
			return synchronize_cache(info);*/
	}

	return B_DEV_INVALID_IOCTL;
}


static driver_module_info sVirtioBlockDriver = {
	.info = {
		.name = VIRTIO_BLOCK_DRIVER_MODULE_NAME,
	},
	.probe = VirtioBlockDriver::Probe
};


module_info* modules[] = {
	(module_info* )&sVirtioBlockDriver,
	NULL
};
