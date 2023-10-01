/*
 * Copyright 2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

#include "ExternalAllocator.h"

#include <new>
#include <stdlib.h>


template <typename T> T
RoundDown(T a, T b)
{
	return a / b * b;
}


template <typename T> T
RoundUp(T a, T b)
{
	return RoundDown(a + b - 1, b);
}


ExternalAllocator::~ExternalAllocator()
{
	fSizeMap.Clear();
	for (;;) {
		Block* block = fAdrMap.LeftMost();
		if (block == NULL) break;
		fAdrMap.Remove(block);
		delete block;
	}
}


status_t
ExternalAllocator::Register(uint64 adr, uint64 size)
{
	Block* block = new(std::nothrow) Block(adr, size);
	if (block == NULL)
		return B_NO_MEMORY;

	fAdrMap.Insert(block);
	fSizeMap.Insert(block);
	fTotalSize += size;

	return B_OK;
}


status_t
ExternalAllocator::Alloc(uint64& adr, uint64 size)
{
	Block* block = fSizeMap.FindClosest(size, false);
	if (block == NULL)
		return ENOENT;

	if (block->fSize > size) {
		Block* remainingBlock = new(std::nothrow) Block(block->fAdr + size, block->fSize - size);
		if (remainingBlock == NULL)
			return B_NO_MEMORY;

		fSizeMap.Remove(block);
		fAdrMap.Insert(remainingBlock);
		fSizeMap.Insert(remainingBlock);
		block->fSize = size;
	} else {
		fSizeMap.Remove(block);
	}

	block->fAllocated = true;
	adr = block->fAdr;
	fAllocSize += block->fSize;
	return B_OK;
}


status_t
ExternalAllocator::AllocAligned(uint64& adr, uint64 size, uint64 align)
{
	Block* block = fSizeMap.FindClosest(size, false);
	for (;;) {
		if (block == NULL)
			return ENOENT;

		uint64 retPtr = RoundUp(block->fAdr, align);
		status_t res = AllocAt(retPtr, size);
		if (res >= B_OK) {
			adr = retPtr;
			return B_OK;
		}
		if (res == ENOENT)
			block = fSizeMap.Next(block);
		else
			return res;
	}
}


status_t
ExternalAllocator::AllocAt(uint64 adr, uint64 size)
{
	Block* block = fAdrMap.FindClosest(adr, true);

	if (block == NULL || block->fAllocated || !(adr >= block->fAdr && adr + size <= block->fAdr + block->fSize))
		return ENOENT;

	uint64 sizeBefore = adr - block->fAdr;
	uint64 sizeAfter = block->fAdr + block->fSize - (adr + size);

	Block* blockBefore = NULL;
	if (sizeBefore > 0) {
		blockBefore = new(std::nothrow) Block(block->fAdr, sizeBefore);
		if (blockBefore == NULL)
			return B_NO_MEMORY;
	}

	Block* blockAfter = NULL;
	if (sizeAfter > 0) {
		blockAfter = new(std::nothrow) Block(adr + size, sizeAfter);
		if (blockAfter == NULL)
			return B_NO_MEMORY;
	}

	fAdrMap.Remove(block);
	fSizeMap.Remove(block);

	if (sizeBefore > 0) {
		fAdrMap.Insert(blockBefore);
		fSizeMap.Insert(blockBefore);
	}

	if (sizeAfter > 0) {
		fAdrMap.Insert(blockAfter);
		fSizeMap.Insert(blockAfter);
	}

	block->fAdr = adr;
	block->fSize = size;
	fAdrMap.Insert(block);

	block->fAllocated = true;
	fAllocSize += block->fSize;
	return B_OK;
}


status_t
ExternalAllocator::Free(uint64 adr)
{
	Block* block = fAdrMap.Find(adr);
	if (block == NULL || !block->fAllocated)
		return ENOENT;

	fSizeMap.Insert(block);
	block->fAllocated = false;
	fAllocSize -= block->fSize;

	Block* prev = fAdrMap.Previous(block);
	if (prev != NULL && !prev->fAllocated) {
		fSizeMap.Remove(prev);
		prev->fSize += block->fSize;

		fAdrMap.Remove(block);
		fSizeMap.Remove(block);
		delete block;

		block = prev;
		fSizeMap.Insert(block);
	}

	Block* next = fAdrMap.Next(block);
	if (next != NULL && !next->fAllocated) {
		fSizeMap.Remove(block);
		block->fSize += next->fSize;

		fAdrMap.Remove(next);
		fSizeMap.Remove(next);
		delete next;

		fSizeMap.Insert(block);
	}

	return B_OK;
}
