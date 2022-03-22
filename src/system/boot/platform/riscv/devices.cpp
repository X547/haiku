/*
 * Copyright 2003-2006, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "bios.h"
#include "virtio.h"

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


void* aligned_malloc(size_t required_bytes, size_t alignment);
void aligned_free(void* p);


class VirtioBlockDevice : public Node
{
	public:
		VirtioBlockDevice(VirtioDevice* blockIo);
		virtual ~VirtioBlockDevice();

		virtual ssize_t ReadAt(void* cookie, off_t pos, void* buffer,
			size_t bufferSize);
		virtual ssize_t WriteAt(void* cookie, off_t pos, const void* buffer,
			size_t bufferSize) { return B_UNSUPPORTED; }
		virtual off_t Size() const {
			return (*(uint32*)(&fBlockIo->Regs()->config[0])
				+ ((uint64)(*(uint32*)(&fBlockIo->Regs()->config[4])) << 32)
				)*kVirtioBlockSectorSize;
		}

		uint32 BlockSize() const { return kVirtioBlockSectorSize; }
		bool ReadOnly() const { return false; }
	private:
		ObjectDeleter<VirtioDevice> fBlockIo;
};


VirtioBlockDevice::VirtioBlockDevice(VirtioDevice* blockIo)
	:
	fBlockIo(blockIo)
{
	dprintf("+VirtioBlockDevice\n");
}


VirtioBlockDevice::~VirtioBlockDevice()
{
	dprintf("-VirtioBlockDevice\n");
}


ssize_t
VirtioBlockDevice::ReadAt(void* cookie, off_t pos, void* buffer,
	size_t bufferSize)
{
	// dprintf("ReadAt(%p, %ld, %p, %ld)\n", cookie, pos, buffer, bufferSize);

	off_t offset = pos % BlockSize();
	pos /= BlockSize();

	uint32 numBlocks = (offset + bufferSize + BlockSize() - 1) / BlockSize();

	ArrayDeleter<char> readBuffer(
		new(std::nothrow) char[numBlocks * BlockSize() + 1]);
	if (!readBuffer.IsSet())
		return B_NO_MEMORY;

	VirtioBlockRequest blkReq;
	blkReq.type = kVirtioBlockTypeIn;
	blkReq.ioprio = 0;
	blkReq.sectorNum = pos;
	IORequest req(ioOpRead, &blkReq, sizeof(blkReq));
	IORequest reply(ioOpWrite, readBuffer.Get(), numBlocks * BlockSize() + 1);
	IORequest* reqs[] = {&req, &reply};
	fBlockIo->ScheduleIO(reqs, 2);
	fBlockIo->WaitIO();

	if (readBuffer[numBlocks * BlockSize()] != kVirtioBlockStatusOk) {
		dprintf("%s: blockIo error reading from device!\n", __func__);
		return B_ERROR;
	}

	memcpy(buffer, readBuffer.Get() + offset, bufferSize);

	return bufferSize;
}


static VirtioBlockDevice*
CreateVirtioBlockDev(int id)
{
	VirtioResources* devRes = ThisVirtioDev(kVirtioDevBlock, id);
	if (devRes == NULL) return NULL;

	ObjectDeleter<VirtioDevice> virtioDev(
		new(std::nothrow) VirtioDevice(*devRes));
	if (!virtioDev.IsSet())
		panic("Can't allocate memory for VirtioDevice!");

	ObjectDeleter<VirtioBlockDevice> device(
		new(std::nothrow) VirtioBlockDevice(virtioDev.Detach()));
	if (!device.IsSet())
		panic("Can't allocate memory for VirtioBlockDevice!");

	return device.Detach();
}


//#pragma mark -

enum {
	ataBaseAdr = 0x59000000,
};

static void port_byte_out(uint16_t reg, uint8_t val)
{
	uint8_t volatile *adr = (uint8_t*)ataBaseAdr + (reg - 0x1f0);
	*adr = val;
}

static void port_long_out(uint16_t reg, uint32_t val)
{
	uint32_t volatile *adr = (uint32_t*)((uint8_t*)ataBaseAdr + (reg - 0x1f0));
	*adr = val;
}

static uint8_t port_byte_in(uint16_t reg)
{
	uint8_t volatile *adr = (uint8_t*)ataBaseAdr + (reg - 0x1f0);
	return *adr;
}

static uint16_t port_word_in(uint16_t reg)
{
	uint16_t volatile *adr = (uint16_t*)((uint8_t*)ataBaseAdr + (reg - 0x1f0));
	return *adr;
}

#define STATUS_BSY 0x80
#define STATUS_RDY 0x40
#define STATUS_DRQ 0x08
#define STATUS_DF 0x20
#define STATUS_ERR 0x01

//This is really specific to out OS now, assuming ATA bus 0 master 
//Source - OsDev wiki
static void ATA_wait_BSY();
static void ATA_wait_DRQ();

void read_sectors_ATA_PIO(void *target_address, uint32_t LBA, uint32_t sector_count)
{
	uint16_t *target = (uint16_t*)target_address;
	while (sector_count > 0) {
		uint8_t cur_sector_count = (uint8_t)std::min<uint32_t>(sector_count, 0xff);

		ATA_wait_BSY();
		port_byte_out(0x1F6,0xE0 | ((LBA >>24) & 0xF));
		port_byte_out(0x1F2, cur_sector_count);
		port_byte_out(0x1F3, (uint8_t) LBA);
		port_byte_out(0x1F4, (uint8_t)(LBA >> 8));
		port_byte_out(0x1F5, (uint8_t)(LBA >> 16)); 
		port_byte_out(0x1F7,0x20); //Send the read command

		for (uint32_t j = 0; j < cur_sector_count; j++) {
			ATA_wait_BSY();
			ATA_wait_DRQ();
			for (int i = 0; i < 256; i++)
				target[i] = port_word_in(0x1F0);
			target += 256;
		}

		sector_count -= cur_sector_count;
		LBA += cur_sector_count;
	}
}

void identify_ATA_PIO(void *target_address)
{
	ATA_wait_BSY();
	port_byte_out(0x1F6, 0xE0);
	port_byte_out(0x1F2, 1);
	port_byte_out(0x1F3, 0);
	port_byte_out(0x1F4, 0);
	port_byte_out(0x1F5, 0); 
	port_byte_out(0x1F7, 0xEC); // command: identify

	uint16_t *target = (uint16_t*)target_address;

	ATA_wait_BSY();
	ATA_wait_DRQ();
	for (int i = 0; i < 256; i++)
		target[i] = port_word_in(0x1F0);
}

void write_sectors_ATA_PIO(uint32_t LBA, uint8_t sector_count, uint32_t* bytes)
{
	ATA_wait_BSY();
	port_byte_out(0x1F6,0xE0 | ((LBA >>24) & 0xF));
	port_byte_out(0x1F2, sector_count);
	port_byte_out(0x1F3, (uint8_t) LBA);
	port_byte_out(0x1F4, (uint8_t)(LBA >> 8));
	port_byte_out(0x1F5, (uint8_t)(LBA >> 16)); 
	port_byte_out(0x1F7, 0x30); // Send the write command

	for (int j = 0; j < sector_count; j++) {
		ATA_wait_BSY();
		ATA_wait_DRQ();
		for(int i = 0; i < 256; i++) {
			port_long_out(0x1F0, bytes[i]);
		}
	}
}

static void ATA_wait_BSY()   //Wait for bsy to be 0
{
	while (port_byte_in(0x1F7) & STATUS_BSY) {}
}

static void ATA_wait_DRQ()  //Wait fot drq to be 1
{
	while (!(port_byte_in(0x1F7) & STATUS_RDY)) {}
}


class AtaBlockDevice : public Node {
public:
	AtaBlockDevice();
	virtual ~AtaBlockDevice();

	virtual ssize_t ReadAt(void* cookie, off_t pos, void* buffer,
		size_t bufferSize);
	virtual ssize_t WriteAt(void* cookie, off_t pos, const void* buffer,
		size_t bufferSize) { return B_UNSUPPORTED; }
	virtual off_t Size() const {
		return fSize;
	}

	uint32 BlockSize() const { return 512; }
	bool ReadOnly() const { return false; }

private:
	off_t fSize;	
};


AtaBlockDevice::AtaBlockDevice()
{
	dprintf("+AtaBlockDevice\n");

	uint16 info[512 / 2];
	identify_ATA_PIO(&info);

	fSize = ((off_t)info[57] + (((off_t)info[58]) << 16)) * BlockSize();
	dprintf("  size: %" B_PRIu64 "\n", fSize);
}


AtaBlockDevice::~AtaBlockDevice()
{
	dprintf("-AtaBlockDevice\n");
}


ssize_t
AtaBlockDevice::ReadAt(void* cookie, off_t pos, void* buffer,
	size_t bufferSize)
{
	//dprintf("ReadAt(%p, %ld, %p, %ld)\n", cookie, pos, buffer, bufferSize);

	off_t offset = pos % BlockSize();
	pos /= BlockSize();

	uint32 numBlocks = (offset + bufferSize + BlockSize() - 1) / BlockSize();

	ArrayDeleter<char> readBuffer(new(std::nothrow) char[numBlocks * BlockSize()]);
	if (!readBuffer.IsSet())
		return B_NO_MEMORY;

	//dprintf("read_sectors_ATA_PIO(%#" B_PRIx64 ", %" B_PRIu32 ")\n", pos, numBlocks);
	read_sectors_ATA_PIO(readBuffer.Get(), (uint32_t)pos, numBlocks);

	memcpy(buffer, readBuffer.Get() + offset, bufferSize);

	return bufferSize;
}


static AtaBlockDevice*
CreateAtaBlockDev()
{
	ObjectDeleter<AtaBlockDevice> device(
		new(std::nothrow) AtaBlockDevice());
	if (!device.IsSet())
		panic("Can't allocate memory for AtaBlockDevice!");

	return device.Detach();
}

//#pragma mark -


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
	for (int i = 0;; i++) {
		ObjectDeleter<Node> device(false ? (Node*)CreateVirtioBlockDev(i) : (i == 0 ? (Node*)CreateAtaBlockDev() : NULL));
		if (!device.IsSet()) break;
		dprintf("virtio_block[%d]\n", i);
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
