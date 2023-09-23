#include "mmc_disk.h"

#include <stdio.h>
#include <new>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <fs/devfs.h>

#include "mmc_icon.h"


#define VIRTIO_BLOCK_DRIVER_MODULE_NAME "drivers/disk/mmc_disk/driver/v1"


static const uint32 sFreqBase[] = {
	10000,
	100000,
	1000000,
	10000000,
};

static const uint8 sFreqMult[] = {
	0,
	10,
	12,
	13,
	15,
	20,
	25,
	30,
	35,
	40,
	45,
	50,
	55,
	60,
	70,
	80,
};

struct MmcDiskCsd {
	uint32 csd[4];
	bool isHighCapacity;

	uint32 Version() const {return (csd[3] >> 26) & 0xf;}
	uint32 FreqBase() const {return sFreqBase[csd[3] & 0x7];}
	uint32 FreqMult() const {return sFreqMult[(csd[3] >> 3) & 0xf];}
	uint32 Freq() const {return FreqBase() * FreqMult();}
	uint32 DsrImp() const {return (csd[2] >> 12) & 0x1;}
	uint32 ReadBlLen() const {return 1 << ((csd[2] >> 16) & 0xf);}
	uint32 WriteBlLen() const {return 1 << ((csd[0] >> 22) & 0xf);}
	uint32 CSize() const {
		return isHighCapacity
			? (csd[2] & 0x3f) << 16 | (csd[1] & 0xffff0000) >> 16
			: (csd[2] & 0x3ff) << 2 | (csd[1] & 0xc0000000) >> 30;
	}
	uint32 CMult() const {return isHighCapacity ? 8 : (csd[1] & 0x00038000) >> 15;}
	uint64 Capacity() const {
		uint64 capacity = (CSize() + 1) << (CMult() + 2);
		capacity *= ReadBlLen();
		return capacity;
	}
};

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

	fMmcDevice = fNode->QueryBusInterface<MmcDevice>();
	fMmcBus = fMmcDevice->GetBus();

	CHECK_RET(fNode->FindAttrUint16(MMC_DEVICE_RCA, &fRca));
	CHECK_RET(fNode->FindAttrUint8(MMC_DEVICE_TYPE, &fCardType));
	dprintf("  rca: %#04" B_PRIx16 "\n", fRca);
	dprintf("  cardType: ");
	switch (fCardType) {
		case CARD_TYPE_MMC:
			dprintf("MMC");
			break;
		case CARD_TYPE_SD:
			dprintf("SD");
			break;
		case CARD_TYPE_SDHC:
			dprintf("SDHC");
			break;
		case CARD_TYPE_UHS1:
			dprintf("UHS1");
			break;
		case CARD_TYPE_UHS2:
			dprintf("UHS2");
			break;
		case CARD_TYPE_SDIO:
			dprintf("SDIO");
			break;
		default:
			dprintf("?(%" B_PRIu8 ")", fCardType);
			break;
	}
	dprintf("\n");

	switch (fCardType) {
		case CARD_TYPE_SDHC:
		case CARD_TYPE_UHS1:
		case CARD_TYPE_UHS2:
			fIsHighCapacity = true;
			break;
		default:
			fIsHighCapacity = false;
			break;
	}
	dprintf("  isHighCapacity: %d\n\n", fIsHighCapacity);

	// TODO: is this correct?
	switch (fCardType) {
		case CARD_TYPE_SD:
		case CARD_TYPE_MMC:
		case CARD_TYPE_UHS2:
			fIoCommandOffsetAsSectors = false;
			break;
		default:
			fIoCommandOffsetAsSectors = true;
			break;
	}

	MmcDiskCsd csd {.isHighCapacity = fIsHighCapacity};
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SEND_CSD, fRca << 16, csd.csd));

	dprintf("  version: %" B_PRIu32 "\n", csd.Version());
	dprintf("  freqBase: %" B_PRIu32 "\n", csd.FreqBase());
	dprintf("  freqMult: %" B_PRIu32 "\n", csd.FreqMult());
	dprintf("  freq: %" B_PRIu32 "\n", csd.Freq());
	dprintf("  dsrImp: %" B_PRIu32 "\n", csd.DsrImp());
	dprintf("  readBlLen: %" B_PRIu32 "\n", csd.ReadBlLen());
	dprintf("  writeBlLen: %" B_PRIu32 "\n", csd.WriteBlLen());
	dprintf("  csize: %" B_PRIu32 "\n", csd.CSize());
	dprintf("  cmult: %" B_PRIu32 "\n", csd.CMult());
	dprintf("  capacity: %" B_PRIu64 "\n", csd.Capacity());

	fCapacity = csd.Capacity();
	fBlockSize = csd.ReadBlLen();
	fPhysicalBlockSize = csd.ReadBlLen();

	fMmcBus->SetClock(25000);

	uint32 response;
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SELECT_DESELECT_CARD, fRca << 16, &response));

	const uint32 k4BitMode = 2;
	CHECK_RET(fMmcBus->ExecuteCommand(SD_APP_CMD, fRca << 16, &response));
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SET_BUS_WIDTH, k4BitMode, &response));
	CHECK_RET(fMmcBus->SetBusWidth(4));


#if 0
	CHECK_RET(fMmcBus->ExecuteCommand(SD_APP_CMD, fRca << 16, &response));

	uint32 scr[2];
	{
		mmc_command cmd {
			.command = SD_SEND_SCR,
			.response = &response
		};
		mmc_data_bounce data {
			.isWrite = false,
			.blockSize = 8,
			.dataSize = sizeof(scr),
			.data = (uint8*)&scr[0]
		};
		CHECK_RET(fMmcBus->ExecuteCommand(cmd, data));
		dprintf("  scr: %#08" B_PRIx32 ", %#08" B_PRIx32 "\n", scr[0], scr[1]);
	}

	CHECK_RET(fMmcBus->ExecuteCommand(SD_SET_BUS_WIDTH, 0xfffff1, &response));

	CHECK_RET(fMmcBus->ExecuteCommand(SD_APP_CMD, fRca << 16, &response));
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SET_BUS_WIDTH, 2, &response));
	CHECK_RET(fMmcBus->SetBusWidth(4));
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SET_BUS_WIDTH, 0x80fffff1, &response));
	CHECK_RET(fMmcBus->SetClock(49500));

	CHECK_RET(fMmcBus->ExecuteCommand(SD_APP_CMD, fRca << 16, &response));
	CHECK_RET(fMmcBus->ExecuteCommand(SD_SEND_STATUS, 0, &response));
#endif

	fDmaResource.SetTo(new(std::nothrow) DMAResource);
	if (!fDmaResource.IsSet())
		return B_NO_MEMORY;

	dma_restrictions restrictions {
		.high_address = 0xffffffff,
		.max_segment_count = 256,
		.max_segment_size = fBlockSize * 8
	};
	CHECK_RET(fDmaResource->Init(restrictions, fBlockSize, 1024, 32));

	fIoScheduler.SetTo(new(std::nothrow) IOSchedulerSimple(fDmaResource.Get()));
	if (!fIoScheduler.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fIoScheduler->Init("mmc"));
	fIoScheduler->SetCallback(*static_cast<IOCallback*>(this));

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
	CALLED();

	auto DoIO = [this, operation]() -> status_t {
		uint32 response;

		CHECK_RET(fMmcBus->ExecuteCommand({
			.command = SD_SET_BLOCKLEN,
			.argument = fBlockSize,
			.response = &response},
		NULL));

		mmc_command cmd {
			.command = operation->IsWrite() ? SD_WRITE_MULTIPLE_BLOCKS : SD_READ_MULTIPLE_BLOCKS,
			.argument = (uint32)(operation->Offset() / fBlockSize),
			.response = &response
		};
		mmc_data data {
			.isWrite = operation->IsWrite(),
			.blockSize = fBlockSize,
			.blockCnt = (uint32)(operation->Length() / fBlockSize),
			.vecCount = operation->VecCount(),
			.vecs = operation->Vecs()
		};
		CHECK_RET(fMmcBus->ExecuteCommand(cmd, &data));

		CHECK_RET(fMmcBus->ExecuteCommand({.command = SD_STOP_TRANSMISSION, .response = &response}, NULL));

		return B_OK;
	};

	status_t res = DoIO();
	fIoScheduler->OperationCompleted(operation, res, res < B_OK ? 0 : operation->Length());
	return res;
}


status_t
MmcDiskDriver::GetGeometry(device_geometry* geometry)
{
	CALLED();

	devfs_compute_geometry_size(geometry, fCapacity / fBlockSize, fBlockSize);
	geometry->bytes_per_physical_sector = fPhysicalBlockSize;

	geometry->device_type = B_DISK;
	geometry->removable = true; // TODO: detect eMMC which isn't

	geometry->read_only = false; // TODO: check write protect switch?
	geometry->write_once = false;

	return B_OK;
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
	CALLED();

	return fDriver.fIoScheduler->ScheduleRequest(request);
}


status_t
MmcDiskDevFsNodeHandle::Control(uint32 op, void *buffer, size_t length, bool isKernel)
{
	CALLED();

	TRACE("ioctl(op = %" B_PRIu32 ")\n", op);

	switch (op) {
		case B_GET_MEDIA_STATUS:
		{
			if (buffer == NULL || length < sizeof(status_t))
				return B_BAD_VALUE;

			*(status_t *)buffer = B_OK;
			return B_OK;
		}

		case B_GET_DEVICE_SIZE:
		{
			// Legacy ioctl, use B_GET_GEOMETRY

			uint64_t size = fDriver.fCapacity;
			if (size > SIZE_MAX)
				return B_NOT_SUPPORTED;
			size_t size32 = size;
			return user_memcpy(buffer, &size32, sizeof(size_t));
		}

		case B_GET_GEOMETRY:
		{
			if (buffer == NULL || length > sizeof(device_geometry))
				return B_BAD_VALUE;

		 	device_geometry geometry {};
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
