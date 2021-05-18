#include "VirtioDevice.h"

#include <malloc.h>
#include <string.h>
#include <new>

#include <KernelExport.h>
#include <kernel.h>
#include <debug.h>


static inline void SetLowHi(uint32 &low, uint32 &hi, uint64 val)
{
	low = (uint32)val;
	hi  = (uint32)(val >> 32);
}

VirtioQueue::VirtioQueue(VirtioDevice *dev, int32_t id): fDev(dev), fId(id)
{
	fDev->fRegs->queueSel = id;
//	dprintf("queueNumMax: "); dprintf("%d", fRegs->queueNumMax); dprintf("\n");
	fQueueLen = fDev->fRegs->queueNumMax;
	fDev->fRegs->queueNum = fQueueLen;
	fLastUsed = 0;
	
	size_t queueMemSize = 0;
	fDescs = (VirtioDesc*)queueMemSize;  queueMemSize += ROUNDUP(sizeof(VirtioDesc)*fQueueLen, B_PAGE_SIZE);
	fAvail = (VirtioAvail*)queueMemSize; queueMemSize += ROUNDUP(sizeof(VirtioAvail) + sizeof(uint16_t)*fQueueLen, B_PAGE_SIZE);
	fUsed  = (VirtioUsed*)queueMemSize;  queueMemSize += ROUNDUP(sizeof(VirtioUsed) + sizeof(VirtioUsedItem)*fQueueLen, B_PAGE_SIZE);
	
	uint8* queueMem = NULL;
	fArea.SetTo(create_area("VirtIO Queue", (void**)&queueMem, B_ANY_KERNEL_ADDRESS, queueMemSize, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));
	if (!fArea.IsSet())
		panic("can't create area: %08" B_PRIx32, fArea.Get());
	
	physical_entry pe;
	if(get_memory_map(queueMem, queueMemSize, &pe, 1) < B_OK)
		panic("get_memory_map failed");

	dprintf("queueMem: %p\n", queueMem);

	memset(queueMem, 0, queueMemSize);
	
	fDescs = (VirtioDesc*) ((uint8*)fDescs + (size_t)queueMem);
	fAvail = (VirtioAvail*)((uint8*)fAvail + (size_t)queueMem);
	fUsed  = (VirtioUsed*) ((uint8*)fUsed +  (size_t)queueMem);

	dprintf("fDescs: %p\n", fDescs);
	dprintf("fAvail: %p\n", fAvail);
	dprintf("fUsed:  %p\n", fUsed);
	
	phys_addr_t descsPhys = (addr_t)fDescs - (addr_t)queueMem + pe.address;
	phys_addr_t availPhys = (addr_t)fAvail - (addr_t)queueMem + pe.address;
	phys_addr_t usedPhys  = (addr_t)fUsed  - (addr_t)queueMem + pe.address;

	dprintf("descsPhys: %08" B_PRIxADDR "\n", descsPhys);
	dprintf("availPhys: %08" B_PRIxADDR "\n", availPhys);
	dprintf("usedPhys:  %08" B_PRIxADDR "\n", usedPhys);
	
	SetLowHi(fDev->fRegs->queueDescLow,  fDev->fRegs->queueDescHi,  descsPhys);
	SetLowHi(fDev->fRegs->queueAvailLow, fDev->fRegs->queueAvailHi, availPhys);
	SetLowHi(fDev->fRegs->queueUsedLow,  fDev->fRegs->queueUsedHi,  usedPhys);

	// panic("(!)@!areas");

	fFreeDescs.SetTo(new(std::nothrow) uint32_t[(fQueueLen + 31)/32]);
	memset(fFreeDescs.Get(), 0xff, sizeof(uint32_t)*((fQueueLen + 31)/32));
	fReqs.SetTo(new(std::nothrow) VirtioRequest*[fQueueLen]);

	fDev->fRegs->queueReady = 1;
}

VirtioQueue::~VirtioQueue()
{
	free(fDescs); fDescs = NULL;
	free(fAvail); fAvail = NULL;
	free(fUsed); fUsed = NULL;
}


int32_t VirtioQueue::AllocDesc()
{
	for (size_t i = 0; i < fQueueLen; i++) {
		if ((fFreeDescs[i/32] & (1 << (i % 32))) != 0) {
			fFreeDescs[i/32] &= ~((uint32_t)1 << (i % 32));
			return i;
		}
	}
	return -1;
}

void VirtioQueue::FreeDesc(int32_t idx)
{
	fFreeDescs[idx/32] |= (uint32_t)1 << (idx % 32);
}


VirtioDevice::VirtioDevice()
{
	mutex_init(&fLock, "virtio_mmio device");
}

void VirtioDevice::Init(phys_addr_t regs, size_t regsLen, int32 irq, int32_t queueCnt)
{
	fConfigHandler = NULL;
	fQueueHandler = NULL;

	fRegsArea.SetTo(map_physical_memory(
		"Virtio MMIO",
		regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void **)&fRegs
	));
	ASSERT(fRegsArea.IsSet());

	fIrq = irq;

	// Reset
	fRegs->status = 0;

/*
	fRegs->status |= virtioConfigSAcknowledge;
	fRegs->status |= virtioConfigSDriver;
	dprintf("0x%" B_PRIxSIZE, (size_t)fRegs); dprintf(".features: "); dprintf("0x%x", fRegs->deviceFeatures); dprintf("\n");
	fRegs->status |= virtioConfigSFeaturesOk;
	fRegs->status |= virtioConfigSDriverOk;
	fRegs->guestPageSize = 4096;

	fQueueCnt = queueCnt;
	fQueues = new(std::nothrow) VirtioQueue*[fQueueCnt];
	for (int32_t i = 0; i < fQueueCnt; i++) {
		fQueues[i] = new(std::nothrow) VirtioQueue(i, fRegs);
	}
*/
}

void VirtioDevice::ScheduleIO(int32_t queueId, VirtioRequest **reqs, uint32_t cnt)
{
	if (cnt < 1) return;
	if (!(queueId >= 0 && queueId < fQueueCnt)) abort();
	VirtioQueue *queue = fQueues[queueId];
	fRegs->queueSel = queueId;
	int32_t firstDesc, lastDesc;
	for (uint32_t i = 0; i < cnt; i++) {
		int32_t desc = queue->AllocDesc();
		if (desc < 0) {abort(); return;}
		if (i == 0) {
			firstDesc = desc;
		} else {
			queue->fDescs[lastDesc].flags |= vringDescFlagsNext;
			queue->fDescs[lastDesc].next = desc;
			reqs[i - 1]->next = reqs[i];
		}
		queue->fDescs[desc].addr = (uint64_t)(reqs[i]->buf);
		queue->fDescs[desc].len = reqs[i]->len;
		queue->fDescs[desc].flags = 0;
		queue->fDescs[desc].next = 0;
		switch (reqs[i]->op) {
		case ioOpRead: queue->fDescs[desc].flags |= vringDescFlagsWrite; break;
		case ioOpWrite: break;
		}
		reqs[i]->state = ioStatePending;
		lastDesc = desc;
	}
	int32_t idx = queue->fAvail->idx % queue->fQueueLen;
	queue->fReqs[idx] = reqs[0];
	queue->fAvail->ring[idx] = firstDesc;
	queue->fAvail->idx++;
	fRegs->queueNotify = queueId;
}

void VirtioDevice::ScheduleIO(int32_t queueId, VirtioRequest *req)
{
	ScheduleIO(queueId, &req, 1);
}

VirtioRequest *VirtioDevice::ConsumeIO(int32_t queueId)
{
	if (!(queueId >= 0 && queueId < fQueueCnt)) abort();
	VirtioQueue *queue = fQueues[queueId];
	fRegs->queueSel = queueId;
	if (queue->fUsed->idx == queue->fLastUsed) return NULL;
	VirtioRequest *req = queue->fReqs[queue->fLastUsed % queue->fQueueLen]; queue->fReqs[queue->fLastUsed % queue->fQueueLen] = NULL;
	req->len = queue->fUsed->ring[queue->fLastUsed % queue->fQueueLen].len;
	req->state = ioStateDone;
	int32_t desc = queue->fUsed->ring[queue->fLastUsed % queue->fQueueLen].id;
	// dprintf("queue->fUsed->ring[queue->fLastUsed % queue->fQueueLen].len: "); dprintf("%d", queue->fUsed->ring[queue->fLastUsed % queue->fQueueLen].len); dprintf("\n");
	while (vringDescFlagsNext & queue->fDescs[desc].flags) {
		int32_t nextDesc = queue->fDescs[desc].next;
		queue->FreeDesc(desc);
		desc = nextDesc;
	}
	queue->FreeDesc(desc);
	queue->fLastUsed++;
	return req;
}
