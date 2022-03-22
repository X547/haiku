/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioBlockDevice.h"

#include <string.h>


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


VirtioBlockDevice*
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
