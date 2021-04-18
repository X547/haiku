/*
 * Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "cpu.h"

#include <OS.h>
#include <boot/platform.h>
#include <boot/stdio.h>
#include <boot/kernel_args.h>
#include <boot/stage2.h>
#include <arch/cpu.h>
#include <arch_kernel.h>
#include <arch_system_info.h>

#include <string.h>


//#define TRACE_CPU
#ifdef TRACE_CPU
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

enum {
	mstatusFsMask  = 3 << 13,
};

#define Mstatus() ({uint64_t x; asm volatile("csrr %0, mstatus" : "=r" (x)); x;})
#define SetMstatus(x) {asm volatile("csrw mstatus, %0" : : "r" (x));}


//	#pragma mark -


extern "C" void cpu_init()
{
	gKernelArgs.num_cpus = 1;

	// enable FPU
	SetMstatus(Mstatus() | mstatusFsMask);
}


extern "C" void
platform_load_ucode(BootVolume& volume)
{
}
