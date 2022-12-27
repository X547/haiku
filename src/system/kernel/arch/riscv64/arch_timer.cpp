/*
 * Copyright 2019-2021, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Adrien Destugues <pulkomandy@pulkomandy.tk>
 */


#include <arch_cpu_defs.h>
#include <arch_int.h>
#include <arch/timer.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <kernel.h>
#include <platform/sbi/sbi_syscalls.h>
#include <timer.h>
#include <Clint.h>

#include <smp.h>


extern uint32 gPlatform;

static uint64 sTimerConversionFactor;


void
arch_timer_set_hardware_timer(bigtime_t timeout)
{
	// dprintf("arch_timer_set_hardware_timer(%" B_PRIu64 "), cpu: %" B_PRId32 "\n", timeout, smp_get_current_cpu());

	uint64 scaledTimeout = (static_cast<__uint128_t>(timeout) * sTimerConversionFactor) >> 32;
	// dprintf("  scaledTimeout: %" B_PRIu64 "\n", scaledTimeout);

	SetBitsSie(1 << sTimerInt);

	switch (gPlatform) {
		case kPlatformMNative:
			MSyscall(kMSyscallSetTimer, true, gClintRegs->mtime + scaledTimeout);
			break;
		case kPlatformSbi: {
			sbi_set_timer(CpuTime() + scaledTimeout);
			break;
		}
		default:
			;
	}
}


void
arch_timer_clear_hardware_timer()
{
	ClearBitsSie(1 << sTimerInt);

	switch (gPlatform) {
		case kPlatformMNative:
			MSyscall(kMSyscallSetTimer, false);
			break;
		default:
			;
	}
}


int
arch_init_timer(kernel_args *args)
{
	dprintf("arch_init_timer\n");

	sTimerConversionFactor = (1LL << 32) * args->arch_args.timerFrequency / 1000000LL;
	dprintf("  sTimerConversionFactor: %" B_PRIu64 "\n", sTimerConversionFactor);

	return B_OK;
}
