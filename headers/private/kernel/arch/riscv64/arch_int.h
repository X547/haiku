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
	SetSstatus(Sstatus() | ARCH_SR_SIE);
}


static inline int
arch_int_disable_interrupts_inline(void)
{
	uint64 sstatus = Sstatus();
	SetSstatus(sstatus & ~ARCH_SR_SIE);
	return (sstatus & ARCH_SR_SIE) != 0;
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
	return (Sstatus() & ARCH_SR_SIE) != 0;
}


// map the functions to the inline versions
#define arch_int_enable_interrupts()	arch_int_enable_interrupts_inline()
#define arch_int_disable_interrupts()	arch_int_disable_interrupts_inline()
#define arch_int_restore_interrupts(status)	\
	arch_int_restore_interrupts_inline(status)
#define arch_int_are_interrupts_enabled()	\
	arch_int_are_interrupts_enabled_inline()


#endif /* _KERNEL_ARCH_RISCV64_INT_H */
