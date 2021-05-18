/*
 * Copyright 2003-2004, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2019, Adrien Destugues, pulkomandy@pulkomandy.tk.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_RISCV64_CPU_H
#define _KERNEL_ARCH_RISCV64_CPU_H


#include <arch/riscv64/arch_thread_types.h>
#include <arch_cpu_defs.h>
#include <kernel.h>


#define CPU_MAX_CACHE_LEVEL	8
#define CACHE_LINE_SIZE		64


inline void set_ac()
{
	return;
	SstatusReg sstatus(Sstatus());
	sstatus.sum = 1;
	SetSstatus(sstatus.val);
}

inline void clear_ac()
{
	return;
	SstatusReg sstatus(Sstatus());
	sstatus.sum = 0;
	SetSstatus(sstatus.val);
}


typedef struct arch_cpu_info {
	int null;
} arch_cpu_info;


#ifdef __cplusplus
extern "C" {
#endif


static inline void
arch_cpu_pause(void)
{
	// TODO: CPU pause
}


static inline void
arch_cpu_idle(void)
{
	Wfi();
}


#ifdef __cplusplus
}
#endif


#endif	/* _KERNEL_ARCH_RISCV64_CPU_H */
