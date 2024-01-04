/*
 * Copyright 2006-2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <arch/generic/debug_uart.h>


void
DebugUART::Out8(int reg, uint8 value)
{
#if defined(__i386__) || defined(__x86_64__)
	// outb for access to IO space.
	if ((Base() + reg) <= 0xFFFF) {
		__asm__ volatile ("outb %%al,%%dx" : : "a" (value), "d" (Base() + reg));
		return;
	}
#endif
	void* address = (uint8*)Base() + (reg << fRegShift);
	switch (fRegIoWidth) {
		case 1:
			*(vint8*)address = value;
			break;
		case 2:
			*(vint16*)address = value;
			break;
		case 4:
			*(vint32*)address = value;
			break;
	}
}


uint8
DebugUART::In8(int reg)
{
#if defined(__i386__) || defined(__x86_64__)
	// inb for access to IO space.
	if ((Base() + reg) <= 0xFFFF) {
		uint8 _v;
		__asm__ volatile ("inb %%dx,%%al" : "=a" (_v) : "d" (Base() + reg));
		return _v;
	}
#endif
	void* address = (uint8*)Base() + (reg << fRegShift);
	switch (fRegIoWidth) {
		case 1:
			return *(vint8*)address;
			break;
		case 2:
			return *(vint16*)address;
			break;
		case 4:
			return *(vint32*)address;
	}
	return 0;
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
