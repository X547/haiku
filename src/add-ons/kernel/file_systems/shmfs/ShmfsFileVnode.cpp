/*
 * _DoCacheIO, _GetPages, _PutPages are based on Haiku `ramfs` code under following license:
 *
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2019, Haiku, Inc.
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "Shmfs.h"

#include <KernelExport.h>
#include <NodeMonitor.h>

#include <vfs.h>
#include <vm/VMCache.h>
#include <vm/vm_page.h>

#include <util/AutoLock.h>

#include <new>
#include <algorithm>


class ShmfsFileCookie {
public:
	bool isAppend = false;
};


status_t ShmfsFileVnode::Init()
{
	CHECK_RET(VMCacheFactory::CreateAnonymousCache(fCache, false, 0, 0, false, VM_PRIORITY_SYSTEM));
	fCache->temporary = true;
	struct vnode* vnode;
	CHECK_RET(vfs_lookup_vnode(Volume()->Id(), Id(), &vnode));
	CHECK_RET(vfs_set_vnode_cache(vnode, fCache));
	return B_OK;
}

ShmfsFileVnode::~ShmfsFileVnode()
{
	if (fCache != NULL) {
		fCache->ReleaseRef();
		fCache = NULL;
	}
}


status_t
ShmfsFileVnode::_DoCacheIO(const off_t offset, uint8* buffer, ssize_t length, size_t &bytesProcessed, bool isWrite)
{
	const size_t originalLength = length;
	const bool user = IS_USER_ADDRESS(buffer);

	const off_t rounded_offset = ROUNDDOWN(offset, B_PAGE_SIZE);
	const size_t rounded_len = ROUNDUP((length) + (offset - rounded_offset),
		B_PAGE_SIZE);
	vm_page** pages = new(std::nothrow) vm_page*[rounded_len / B_PAGE_SIZE];
	if (pages == NULL)
		return B_NO_MEMORY;
	ArrayDeleter<vm_page*> pagesDeleter(pages);

	_GetPages(rounded_offset, rounded_len, isWrite, pages);

	status_t error = B_OK;
	size_t index = 0;

	while (length > 0) {
		vm_page* page = pages[index];
		phys_addr_t at = (page != NULL)
			? (page->physical_page_number * B_PAGE_SIZE) : 0;
		ssize_t bytes = B_PAGE_SIZE;
		if (index == 0) {
			const uint32 pageoffset = (offset % B_PAGE_SIZE);
			at += pageoffset;
			bytes -= pageoffset;
		}
		bytes = std::min<ssize_t>(length, bytes);

		if (isWrite) {
			page->modified = true;
			error = vm_memcpy_to_physical(at, buffer, bytes, user);
		} else {
			if (page != NULL) {
				error = vm_memcpy_from_physical(buffer, at, bytes, user);
			} else {
				if (user) {
					error = user_memset(buffer, 0, bytes);
				} else {
					memset(buffer, 0, bytes);
				}
			}
		}
		if (error != B_OK)
			break;

		buffer += bytes;
		length -= bytes;
		index++;
	}

	_PutPages(rounded_offset, rounded_len, pages, error == B_OK);

	bytesProcessed = length > 0 ? originalLength - length : originalLength;

	return error;
}

void ShmfsFileVnode::_GetPages(off_t offset, off_t length, bool isWrite, vm_page** pages)
{
	// TODO: This method is duplicated in the ram_disk. Perhaps it
	// should be put into a common location?

	// get the pages, we already have
	AutoLocker<VMCache> locker(fCache);

	size_t pageCount = length / B_PAGE_SIZE;
	size_t index = 0;
	size_t missingPages = 0;

	while (length > 0) {
		vm_page* page = fCache->LookupPage(offset);
		if (page != NULL) {
			if (page->busy) {
				fCache->WaitForPageEvents(page, PAGE_EVENT_NOT_BUSY, true);
				continue;
			}

			DEBUG_PAGE_ACCESS_START(page);
			page->busy = true;
		} else
			missingPages++;

		pages[index++] = page;
		offset += B_PAGE_SIZE;
		length -= B_PAGE_SIZE;
	}

	locker.Unlock();

	// For a write we need to reserve the missing pages.
	if (isWrite && missingPages > 0) {
		vm_page_reservation reservation;
		vm_page_reserve_pages(&reservation, missingPages,
			VM_PRIORITY_SYSTEM);

		for (size_t i = 0; i < pageCount; i++) {
			if (pages[i] != NULL)
				continue;

			pages[i] = vm_page_allocate_page(&reservation,
				PAGE_STATE_WIRED | VM_PAGE_ALLOC_BUSY);

			if (--missingPages == 0)
				break;
		}

		vm_page_unreserve_pages(&reservation);
	}
}

void ShmfsFileVnode::_PutPages(off_t offset, off_t length, vm_page** pages, bool success)
{
	// TODO: This method is duplicated in the ram_disk. Perhaps it
	// should be put into a common location?

	AutoLocker<VMCache> locker(fCache);

	// Mark all pages unbusy. On error free the newly allocated pages.
	size_t index = 0;

	while (length > 0) {
		vm_page* page = pages[index++];
		if (page != NULL) {
			if (page->CacheRef() == NULL) {
				if (success) {
					fCache->InsertPage(page, offset);
					fCache->MarkPageUnbusy(page);
					DEBUG_PAGE_ACCESS_END(page);
				} else
					vm_page_free(NULL, page);
			} else {
				fCache->MarkPageUnbusy(page);
				DEBUG_PAGE_ACCESS_END(page);
			}
		}

		offset += B_PAGE_SIZE;
		length -= B_PAGE_SIZE;
	}
}


//#pragma mark - VFS interface

status_t ShmfsFileVnode::SetFlags(ShmfsFileCookie* cookie, int flags)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".FileVnode::SetFlags(%p, %x)\n", Id(), cookie, flags);
	cookie->isAppend = (flags & O_APPEND) != 0;
	return B_OK;
}

status_t ShmfsFileVnode::ReadStat(struct stat &stat)
{
	RecursiveLocker lock(Volume()->Lock());
	CHECK_RET(ShmfsVnode::ReadStat(stat));
	stat.st_mode |= S_IFREG;
	stat.st_size = fDataSize;
	stat.st_blocks = (fDataSize + (512 - 1)) / 512;
	return B_OK;
}

status_t ShmfsFileVnode::WriteStat(const struct stat &stat, uint32 statMask)
{
	RecursiveLocker lock(Volume()->Lock());

	if ((statMask & B_STAT_SIZE) != 0) {
		AutoLocker<VMCache> _(fCache);
		CHECK_RET(fCache->Resize(stat.st_size, VM_PRIORITY_SYSTEM));
		fDataSize = stat.st_size;
	}
	return ShmfsVnode::WriteStat(stat, statMask);
}

status_t ShmfsFileVnode::Open(int openMode, ShmfsFileCookie* &outCookie)
{
	TRACE("#%" B_PRId64 ".FileVnode::Open(%x, &cookie: %p)\n", Id(), openMode, &cookie);
	ObjectDeleter<ShmfsFileCookie> cookie(new(std::nothrow) ShmfsFileCookie());
	if (!cookie.IsSet())
		return B_NO_MEMORY;
	cookie->isAppend = (openMode & O_APPEND) != 0;
	if (fDataSize > 0 && (O_TRUNC & openMode) != 0)
		CHECK_RET(WriteStat({.st_size = 0}, B_STAT_SIZE));
	outCookie = cookie.Detach();
	return B_OK;
}

status_t ShmfsFileVnode::FreeCookie(ShmfsFileCookie* cookie)
{
	TRACE("#%" B_PRId64 ".FileVnode::FreeCookie(%p)\n", Id(), cookie);
	delete cookie;
	return B_OK;
}

status_t ShmfsFileVnode::Read(ShmfsFileCookie* cookie, off_t pos, void* buffer, size_t &outLength)
{
	ino_t dirId;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".FileVnode::Read(%p, %" B_PRId64 ")\n", Id(), cookie, pos);

	if (pos < 0)
		return B_BAD_VALUE;

	pos = std::min<off_t>(pos, fDataSize);
	size_t length = std::min<size_t>(outLength, size_t(fDataSize - pos));

	struct timespec time;
	GetCurrentTime(time);
	fAccessTime = time;

	dirId = fParent == NULL ? 0 : fParent->Id();

	CHECK_RET(_DoCacheIO(pos, (uint8*)buffer, length, outLength, false));
	}
	notify_stat_changed(Volume()->Id(), dirId, Id(), B_STAT_ACCESS_TIME);
	return B_OK;
}

status_t ShmfsFileVnode::Write(ShmfsFileCookie* cookie, off_t pos, const void* buffer, size_t &outLength)
{
	ino_t dirId;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".FileVnode::Write(%p, %" B_PRId64 ")\n", Id(), cookie, pos);

	if (cookie->isAppend)
		pos = fDataSize;

	if (pos < 0)
		return B_BAD_VALUE;

	size_t length = outLength;
	off_t newSize = pos + length;
	if (newSize <= 0) {
		length = 0;
		return B_OK;
	}
	if (newSize > fDataSize) {
		AutoLocker<VMCache> _(fCache);
		CHECK_RET(fCache->Resize(newSize, VM_PRIORITY_SYSTEM));
		fDataSize = newSize;
	}

	struct timespec time;
	GetCurrentTime(time);
	fAccessTime = time;
	fModifyTime = time;

	dirId = fParent == NULL ? 0 : fParent->Id();

	CHECK_RET(_DoCacheIO(pos, (uint8*)buffer, length, outLength, true));
	}
	notify_stat_changed(Volume()->Id(), dirId, Id(), B_STAT_ACCESS_TIME | B_STAT_MODIFICATION_TIME);
	return B_OK;
}
