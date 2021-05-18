/*
 * Copyright 2005-2019, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Axel Dörfler <axeld@pinc-software.de>
 * 		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */
#ifndef _KERNEL_ARCH_RISCV64_INT_H
#define _KERNEL_ARCH_RISCV64_INT_H

#include <SupportDefs.h>
#include <arch_cpu_defs.h>

#define NUM_IO_VECTORS	256


#define Sstatus() ({uint64_t x; asm volatile("csrr %0, sstatus" : "=r" (x)); x;})
#define SetSstatus(x) {asm volatile("csrw sstatus, %0" : : "r" (x));}


static inline void
arch_int_enable_interrupts_inline(void)
{
	SstatusReg status(Sstatus());
	status.ie |= (1 << modeS);
	SetSstatus(status.val);
}


static inline int
arch_int_disable_interrupts_inline(void)
{
	SstatusReg status(Sstatus());
	int oldState = ((1 << modeS) & status.ie) != 0;
	status.ie &= ~(1 << modeS);
	SetSstatus(status.val);
	return oldState;
}


static inline void
arch_int_restore_interrupts_inline(int oldState)
{
	if (oldState)
		arch_int_enable_interrupts_inline();
}


static inline bool
arch_int_are_interrupts_enabled_inline(void)
{
	SstatusReg status(Sstatus());
	return ((1 << modeS) & status.ie) != 0;
}


// map the functions to the inline versions
#define arch_int_enable_interrupts()	arch_int_enable_interrupts_inline()
#define arch_int_disable_interrupts()	arch_int_disable_interrupts_inline()
#define arch_int_restore_interrupts(status)	\
	arch_int_restore_interrupts_inline(status)
#define arch_int_are_interrupts_enabled()	\
	arch_int_are_interrupts_enabled_inline()


enum {
	switchToSmodeMmodeSyscall = 0,
	setTimerMmodeSyscall = 1,
};

extern "C" status_t MSyscall(...);


#endif /* _KERNEL_ARCH_RISCV64_INT_H */
