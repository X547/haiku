/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "AtaBlockDevice.h"

#include <AutoDeleter.h>

#include <string.h>
#include <algorithm>


enum {
	ataBaseAdr = 0x40000000,
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


//#pragma mark -

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


AtaBlockDevice*
CreateAtaBlockDev()
{
	ObjectDeleter<AtaBlockDevice> device(new(std::nothrow) AtaBlockDevice());
	if (!device.IsSet())
		panic("Can't allocate memory for AtaBlockDevice!");

	return device.Detach();
}
