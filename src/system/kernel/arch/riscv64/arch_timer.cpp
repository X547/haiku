/*
 * Copyright 2019, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Adrien Destugues <pulkomandy@pulkomandy.tk>
 */


#include <kernel.h>
#include <debug.h>
#include <timer.h>
#include <arch/timer.h>
#include <arch_int.h>
#include <arch_cpu_defs.h>
#include <Clint.h>


void
arch_timer_set_hardware_timer(bigtime_t timeout)
{
	MSyscall(setTimerMmodeSyscall, true, gClintRegs->mTime + timeout/10);
}


void
arch_timer_clear_hardware_timer()
{
	MSyscall(setTimerMmodeSyscall, false);
}


int
arch_init_timer(kernel_args *args)
{
	return B_OK;
}


bigtime_t
system_time(void)
{
	return CpuTime();
}
