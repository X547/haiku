/*
 * Copyright 2023, Haiku Inc.
 * Distributed under the terms of the MIT License.
 */

#include <string.h>
#include <OS.h>

#include <errno_private.h>

#include "mimalloc.h"


// TODO: Is it needed to have some code in this handlers?

extern "C" void
__heap_before_fork(void)
{
}


extern "C" void
__init_after_fork(void)
{
}


extern "C" void
__heap_after_fork_child(void)
{
}


extern "C" void
__heap_after_fork_parent(void)
{
}


//	#pragma mark - public functions


extern "C" void *
malloc(size_t size)
{
	return mi_malloc(size);
}


extern "C" void *
calloc(size_t nelem, size_t elsize)
{
	return mi_calloc(nelem, elsize);
}


extern "C" void
free(void *ptr)
{
	return mi_free(ptr);
}


extern "C" void *
memalign(size_t alignment, size_t size)
{
	return mi_memalign(alignment, size);
}


extern "C" void *
aligned_alloc(size_t alignment, size_t size)
{
	if (size % alignment != 0) {
		__set_errno(B_BAD_VALUE);
		return NULL;
	}
	return memalign(alignment, size);
}


extern "C" int
posix_memalign(void **_pointer, size_t alignment, size_t size)
{
	return mi_posix_memalign(_pointer, alignment, size);
}


extern "C" void *
valloc(size_t size)
{
	return memalign(B_PAGE_SIZE, size);
}


extern "C" void *
realloc(void *ptr, size_t size)
{
	return mi_realloc(ptr, size);
}


extern "C" size_t
malloc_usable_size(void *ptr)
{
	return mi_malloc_usable_size(ptr);
}


//	#pragma mark - BeOS specific extensions


struct mstats {
	size_t bytes_total;
	size_t chunks_used;
	size_t bytes_used;
	size_t chunks_free;
	size_t bytes_free;
};


extern "C" struct mstats mstats(void);

extern "C" struct mstats
mstats(void)
{
	static struct mstats stats;

	memset(&stats, 0, sizeof(stats));

	// TODO: implement

	return stats;
}
