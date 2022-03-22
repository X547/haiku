/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "NvmeBlockDevice.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


static void
SetLoHi(vuint32 &lo, vuint32 &hi, uint64 val)
{
	lo = (uint32)val;
	hi = (uint32)(val >> 32);
}


static uint64
GetLoHi(uint32 lo, uint32 hi)
{
	return (uint64)lo + (((uint64)hi) << 32);
}


status_t
NvmeBlockDevice::Queue::Init()
{
	submLen = 4096 / sizeof(NvmeSubmissionPacket);
	complLen = 4096 / sizeof(NvmeCompletionPacket);
	submArray.SetTo((NvmeSubmissionPacket*)aligned_malloc(submLen*sizeof(NvmeSubmissionPacket), 4096));
	if (!submArray.IsSet())
		return B_NO_MEMORY;
	complArray.SetTo((NvmeCompletionPacket*)aligned_malloc(complLen*sizeof(NvmeCompletionPacket), 4096));
	if (!complArray.IsSet())
		return B_NO_MEMORY;
	return B_OK;
}


NvmeBlockDevice::NvmeBlockDevice()
{
	fRegs = (volatile NvmeRegs*)(0x40000000);
}


NvmeBlockDevice::~NvmeBlockDevice()
{
}


status_t
NvmeBlockDevice::Init()
{
	dprintf("NvmeBlockDevice::Init()\n");
	CHECK_RET(fQueues[0].Init());
	CHECK_RET(fQueues[1].Init());
	fRegs->adminQueueAttrs.val = NvmeRegs::AdminQueueAttrs{
		.submQueueLen = fQueues[0].submLen,
		.complQueueLen = fQueues[0].complLen
	}.val;
	SetLoHi(fRegs->adminSubmQueueAdrLo, fRegs->adminSubmQueueAdrHi, (addr_t)fQueues[0].submArray.Get());
	SetLoHi(fRegs->adminComplQueueAdrLo, fRegs->adminComplQueueAdrHi, (addr_t)fQueues[0].complArray.Get());

	NvmeSubmissionPacket& packet = fQueues[0].submArray.Get()[fQueues[0].submHead];
	packet = {
		.opcode = nvmeAdminOpCreateSubmQueue
	};

	fQueues[0].submHead = (fQueues[0].submHead + 1) & (fQueues[0].submLen - 1);

	dprintf("  fRegs->cap1: %#" B_PRIx32 "\n", fRegs->cap1);
	dprintf("  fRegs->cap2: %#" B_PRIx32 "\n", fRegs->cap2);
	dprintf("  fRegs->version: %#" B_PRIx32 "\n", fRegs->version);
	dprintf("  fRegs->adminSubmQueue: %#" B_PRIx64 "\n", GetLoHi(fRegs->adminSubmQueueAdrLo, fRegs->adminSubmQueueAdrHi));
	dprintf("  fRegs->adminComplQueue: %#" B_PRIx64 "\n", GetLoHi(fRegs->adminComplQueueAdrLo, fRegs->adminComplQueueAdrHi));
	dprintf("  fRegs->adminQueueAttrs: %" B_PRIu16 ", %" B_PRIu16 "\n", fRegs->adminQueueAttrs.submQueueLen, fRegs->adminQueueAttrs.complQueueLen);
	return B_OK;
}


ssize_t
NvmeBlockDevice::ReadAt(void* cookie, off_t pos, void* buffer, size_t bufferSize)
{
	return B_UNSUPPORTED;
}


ssize_t
NvmeBlockDevice::WriteAt(void* cookie, off_t pos, const void* buffer, size_t bufferSize)
{
	return B_UNSUPPORTED;
}


off_t
NvmeBlockDevice::Size() const
{
	return fSize;
}


NvmeBlockDevice*
CreateNvmeBlockDev()
{
	ObjectDeleter<NvmeBlockDevice> device(new(std::nothrow) NvmeBlockDevice());
	if (!device.IsSet())
		panic("Can't allocate memory for NvmeBlockDevice!");

	status_t res = device->Init();
	if (res < B_OK) {
		dprintf("NvmeBlockDevice initialization failed: %" B_PRIx32 "\n", res);
		return NULL;
	}

	return device.Detach();
}
