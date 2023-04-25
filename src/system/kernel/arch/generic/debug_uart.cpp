/*
 * Copyright 2006-2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <arch/generic/debug_uart.h>


void
DebugUART::Out8(int reg, uint8 value)
{
#if 1
	// 32-bit aligned
	*((uint32 *)Base() + reg) = value;
#else
	*((uint8 *)Base() + reg) = value;
#endif
}


uint8
DebugUART::In8(int reg)
{
#if 1
	// 32-bit aligned
	return *((uint32 *)Base() + reg);
#else
	return *((uint8 *)Base() + reg);
#endif
}


void
DebugUART::Barrier()
{
	// Simple memory barriers
#if defined(__POWERPC__)
	asm volatile("eieio; sync");
#elif defined(__ARM__) || defined(__aarch64__)
	asm volatile ("" : : : "memory");
#endif
}
