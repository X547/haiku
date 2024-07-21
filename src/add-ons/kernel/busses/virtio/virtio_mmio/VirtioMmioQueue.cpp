/*
 * Copyright 2021-2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioMmioDevice.h"

#include <string.h>
#include <new>

#include <kernel.h>


static inline void
SetLowHi(vuint32 &low, vuint32 &hi, uint64 val)
{
	low = (uint32)val;
	hi  = (uint32)(val >> 32);
}


VirtioMmioQueue::VirtioMmioQueue(VirtioMmioDevice *dev, int32 id)
	:
	fDev(dev),
	fId(id)
{
}


status_t
VirtioMmioQueue::Init()
{
	fDev->fRegs->queueSel = fId;
	TRACE("queueNumMax: %d\n", fDev->fRegs->queueNumMax);
	fQueueLen = fDev->fRegs->queueNumMax;
	fDescCount = fQueueLen;
	fDev->fRegs->queueNum = fQueueLen;
	fLastUsed = 0;

	size_t queueMemSize = 0;
	size_t descsOffset = queueMemSize;
	queueMemSize += ROUNDUP(sizeof(VirtioDesc) * fDescCount, B_PAGE_SIZE);

	size_t availOffset = queueMemSize;
	queueMemSize += ROUNDUP(sizeof(VirtioAvail) + sizeof(uint16) * fQueueLen, B_PAGE_SIZE);

	size_t usedOffset = queueMemSize;
	queueMemSize += ROUNDUP(sizeof(VirtioUsed) + sizeof(VirtioUsedItem) * fQueueLen, B_PAGE_SIZE);

	uint8* queueMem = NULL;
	fArea.SetTo(create_area("VirtIO Queue", (void**)&queueMem,
		B_ANY_KERNEL_ADDRESS, queueMemSize, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));

	if (!fArea.IsSet()) {
		ERROR("can't create area: %08" B_PRIx32, fArea.Get());
		return fArea.Get();
	}

	physical_entry pe;
	status_t res = get_memory_map(queueMem, queueMemSize, &pe, 1);
	if (res < B_OK) {
		ERROR("get_memory_map failed");
		return res;
	}

	TRACE("queueMem: %p\n", queueMem);

	memset(queueMem, 0, queueMemSize);

	fDescs = (VirtioDesc*) (queueMem + descsOffset);
	fAvail = (VirtioAvail*)(queueMem + availOffset);
	fUsed  = (VirtioUsed*) (queueMem + usedOffset);

	if (fDev->fRegs->version >= 2) {
		phys_addr_t descsPhys = pe.address + descsOffset;
		phys_addr_t availPhys = pe.address + availOffset;
		phys_addr_t usedPhys  = pe.address + usedOffset;

		SetLowHi(fDev->fRegs->queueDescLow,  fDev->fRegs->queueDescHi,  descsPhys);
		SetLowHi(fDev->fRegs->queueAvailLow, fDev->fRegs->queueAvailHi, availPhys);
		SetLowHi(fDev->fRegs->queueUsedLow,  fDev->fRegs->queueUsedHi,  usedPhys);
	}

	res = fAllocatedDescs.Resize(fDescCount);
	if (res < B_OK)
		return res;

	fCookies.SetTo(new(std::nothrow) void*[fDescCount]);
	if (!fCookies.IsSet())
		return B_NO_MEMORY;

	if (fDev->fRegs->version == 1) {
		uint32_t pfn = pe.address / B_PAGE_SIZE;
		fDev->fRegs->queueAlign = B_PAGE_SIZE;
		fDev->fRegs->queuePfn = pfn;
	} else {
		fDev->fRegs->queueReady = 1;
	}

	return B_OK;
}


int32
VirtioMmioQueue::AllocDesc()
{
	int32 idx = fAllocatedDescs.GetLowestClear();
	if (idx < 0)
		return -1;

	fAllocatedDescs.Set(idx);
	return idx;
}


void
VirtioMmioQueue::FreeDesc(int32 idx)
{
	fAllocatedDescs.Clear(idx);
}


// #pragma mark - Public driver interface

status_t
VirtioMmioQueue::SetupInterrupt(virtio_callback_func handler, void* cookie)
{
	fQueueHandler = handler;
	fQueueHandlerCookie = cookie;
	fQueueHandlerRef.SetTo((handler == NULL) ? NULL : &fDev->fIrqHandler);

	return B_OK;
}


status_t
VirtioMmioQueue::Request(const physical_entry* readEntry, const physical_entry* writtenEntry, void* cookie)
{
	physical_entry vector[2];
	physical_entry* vectorEnd = vector;

	if (readEntry != NULL)
		*vectorEnd++ = *readEntry;

	if (writtenEntry != NULL)
		*vectorEnd++ = *writtenEntry;

	return RequestV(vector, (readEntry != NULL) ? 1 : 0, (writtenEntry != NULL) ? 1 : 0, cookie);
}


status_t
VirtioMmioQueue::RequestV(const physical_entry* vector,
	size_t readVectorCount, size_t writtenVectorCount,
	void* cookie)
{
	int32 firstDesc = -1, lastDesc = -1;
	size_t count = readVectorCount + writtenVectorCount;

	if (count == 0)
		return B_OK;

	for (size_t i = 0; i < count; i++) {
		int32 desc = AllocDesc();

		if (desc < 0) {
			ERROR("no free virtio descs, queue: %p\n", this);

			if (firstDesc >= 0) {
				desc = firstDesc;
				while (kVringDescFlagsNext & fDescs[desc].flags) {
					int32_t nextDesc = fDescs[desc].next;
					FreeDesc(desc);
					desc = nextDesc;
				}
				FreeDesc(desc);
			}

			return B_WOULD_BLOCK;
		}

		if (i == 0) {
			firstDesc = desc;
		} else {
			fDescs[lastDesc].flags |= kVringDescFlagsNext;
			fDescs[lastDesc].next = desc;
		}
		fDescs[desc].addr = vector[i].address;
		fDescs[desc].len = vector[i].size;
		fDescs[desc].flags = 0;
		fDescs[desc].next = 0;
		if (i >= readVectorCount)
			fDescs[desc].flags |= kVringDescFlagsWrite;

		lastDesc = desc;
	}

	int32_t idx = fAvail->idx & (fQueueLen - 1);
	fCookies[firstDesc] = cookie;
	fAvail->ring[idx] = firstDesc;
	fAvail->idx++;
	fDev->fRegs->queueNotify = fId;

	return B_OK;
}


bool
VirtioMmioQueue::IsFull()
{
	panic("not implemented");
	return false;
}


bool
VirtioMmioQueue::IsEmpty()
{
	return fUsed->idx == fLastUsed;
}


uint16
VirtioMmioQueue::Size()
{
	return fQueueLen;
}


bool
VirtioMmioQueue::Dequeue(void** _cookie, uint32* _usedLength)
{
	fDev->fRegs->queueSel = fId;

	if (fUsed->idx == fLastUsed)
		return false;

	int32_t desc = fUsed->ring[fLastUsed & (fQueueLen - 1)].id;

	if (_cookie != NULL)
		*_cookie = fCookies[desc];
	fCookies[desc] = NULL;

	if (_usedLength != NULL)
		*_usedLength = fUsed->ring[fLastUsed & (fQueueLen - 1)].len;

	while (kVringDescFlagsNext & fDescs[desc].flags) {
		int32_t nextDesc = fDescs[desc].next;
		FreeDesc(desc);
		desc = nextDesc;
	}
	FreeDesc(desc);
	fLastUsed++;

	return true;
}
