/*
 * Copyright 2003-2019, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 * 		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 *		Jonas Sundström <jonas@kirilla.com>
 */
#ifndef _KERNEL_ARCH_RISCV64_THREAD_H
#define _KERNEL_ARCH_RISCV64_THREAD_H

#include <arch/cpu.h>

#ifdef __cplusplus
extern "C" {
#endif

void riscv64_push_iframe(struct iframe_stack* stack, struct iframe* frame);
void riscv64_pop_iframe(struct iframe_stack* stack);
struct iframe* riscv64_get_user_iframe(void);


static inline Thread*
arch_thread_get_current_thread(void)
{
	Thread* t;
	asm volatile("mv %0, tp" : "=r" (t));
	return t;
}


static inline void
arch_thread_set_current_thread(Thread* t)
{
	asm volatile("mv tp, %0" : : "r" (t));
}


#ifdef __cplusplus
}
#endif


#endif /* _KERNEL_ARCH_RISCV64_THREAD_H */
