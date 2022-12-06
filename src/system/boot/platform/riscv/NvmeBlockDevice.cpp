/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "NvmeBlockDevice.h"
#include <cstring>

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
	memset(complArray.Get(), 0, complLen * sizeof(NvmeCompletionPacket));
	return B_OK;
}

NvmeSubmissionPacket* NvmeBlockDevice::BeginSubmission(uint32 queueId)
{
	NvmeBlockDevice::Queue* queue = &fQueues[queueId];
	NvmeSubmissionPacket* packet = &queue->submArray.Get()[queue->submHead];
	if (queue->submHead++ >= queue->submLen) queue->submHead = 0;
	memset(packet, 0, sizeof(NvmeSubmissionPacket));
	return packet;
}

void NvmeBlockDevice::CommitSubmissions(uint32 queueId)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	fRegs->doorbell[queueId << 1] = fQueues[queueId].submHead;
}

uint16 NvmeBlockDevice::CompletionStatus(uint32 queueId)
{
	NvmeBlockDevice::Queue* queue = &fQueues[queueId];
	NvmeCompletionPacket* packet = &queue->complArray.Get()[queue->complHead];
	do {
		//dprintf("Polling completion\n");
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
	} while (packet->status.phase == queue->phase);
	if (queue->complHead++ >= queue->complLen) {
		queue->phase = !queue->phase;
		queue->complHead = 0;
	}
	fRegs->doorbell[(queueId << 1) + 1] = queue->complHead;
	return packet->status.status;
}

NvmeBlockDevice::NvmeBlockDevice()
{
	fRegs = (volatile NvmeRegs*)(0x40000000);
}

// Seems like this is never called, and it breaks the next boot stage
NvmeBlockDevice::~NvmeBlockDevice()
{
	dprintf("-NVMe\n");
	// Delete IO queues
	NvmeSubmissionPacket* packet = BeginSubmission(0);
	packet->opcode = nvmeAdminOpDeleteSubmQueue;
	packet->arg1 = 1;
	CommitSubmissions(0);
	if (CompletionStatus(0)) {
		dprintf("Failed to delete IO submission queue\n");
	}
	packet = BeginSubmission(0);
	packet->opcode = nvmeAdminOpDeleteComplQueue;
	packet->arg1 = 1;
	CommitSubmissions(0);
	if (CompletionStatus(0)) {
		dprintf("Failed to delete IO completion queue\n");
	}
	// Reset the controller
	fRegs->ctrlConfig = 0xC000;
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

	dprintf("  fRegs->cap1: %#" B_PRIx32 "\n", fRegs->cap1);
	dprintf("  fRegs->cap2: %#" B_PRIx32 "\n", fRegs->cap2);
	dprintf("  fRegs->version: %#" B_PRIx32 "\n", fRegs->version);
	dprintf("  fRegs->adminSubmQueue: %#" B_PRIx64 "\n", GetLoHi(fRegs->adminSubmQueueAdrLo, fRegs->adminSubmQueueAdrHi));
	dprintf("  fRegs->adminComplQueue: %#" B_PRIx64 "\n", GetLoHi(fRegs->adminComplQueueAdrLo, fRegs->adminComplQueueAdrHi));
	dprintf("  fRegs->adminQueueAttrs: %" B_PRIu16 ", %" B_PRIu16 "\n", fRegs->adminQueueAttrs.submQueueLen, fRegs->adminQueueAttrs.complQueueLen);
	
	NvmeSubmissionPacket* packet = BeginSubmission(0);
	packet->opcode = nvmeAdminOpCreateSubmQueue;
	packet->prp1 = (addr_t)fQueues[1].submArray.Get();
	packet->arg1 = 1 | (fQueues[1].submLen << 16);
	CommitSubmissions(0);
	if (CompletionStatus(0)) {
		dprintf("Failed to create IO submission queue\n");
		return B_UNSUPPORTED;
	}
	
	packet = BeginSubmission(0);
	packet->opcode = nvmeAdminOpCreateComplQueue;
	packet->prp1 = (addr_t)fQueues[1].complArray.Get();
	packet->arg1 = 1 | (fQueues[1].complLen << 16);
	CommitSubmissions(0);
	if (CompletionStatus(0)) {
		dprintf("Failed to create IO completion queue\n");
		return B_UNSUPPORTED;
	}
	
	uint64_t* ident_buff = (uint64_t*)aligned_malloc(4096, 4096);
	packet = BeginSubmission(0);
	packet->opcode = nvmeAdminOpIdentity;
	packet->prp1 = (addr_t)ident_buff;
	CommitSubmissions(0);
	if (CompletionStatus(0)) {
		dprintf("Failed to identify namespace\n");
		aligned_free(ident_buff);
		return B_UNSUPPORTED;
	}
	fSize = ident_buff[0] << 9;
	aligned_free(ident_buff);

	dprintf("  fSize: %#" B_PRIx64 "\n", fSize);
	
	return B_OK;
}

ssize_t
NvmeBlockDevice::ReadAt(void* cookie, off_t pos, void* buffer, size_t bufferSize)
{	
	//dprintf("ReadAt(%p, %ld, %p, %ld)\n", cookie, pos, buffer, bufferSize);
	
	// No PRP support yet, just read sector by sector into a temporary buffer
	// MUST BE PAGE ALIGNED!
	uint64_t* buff = (uint64_t*)aligned_malloc(4096, 4096);
	for (size_t i=0; i<bufferSize; i += 512) {
		NvmeSubmissionPacket* packet = BeginSubmission(1);
		packet->opcode = nvmeOpRead;
		packet->prp1 = (addr_t)buff;
		packet->size = 1;
		packet->arg1 = (pos + i) >> 9;
		CommitSubmissions(1);
		if (CompletionStatus(1)) {
			dprintf("IO error\n");
			aligned_free(buff);
			return B_UNSUPPORTED;
		}
		memcpy(((uint8_t*)buffer) + i, buff, (bufferSize > 512) ? 512 : bufferSize);
	}
	aligned_free(buff);
	
	return bufferSize;
}

ssize_t
NvmeBlockDevice::WriteAt(void* cookie, off_t pos, const void* buffer, size_t bufferSize)
{
	dprintf("WriteAt(%p, %ld, %p, %ld)\n", cookie, pos, buffer, bufferSize);
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
