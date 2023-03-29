/*
 * Copyright 2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _GENERIC_INT_H_
#define _GENERIC_INT_H_

#include <int.h>
#include <arch/int.h>

#ifdef __cplusplus

class InterruptSource {
public:
	virtual void EnableIoInterrupt(int irq) = 0;
	virtual void DisableIoInterrupt(int irq) = 0;
	virtual void ConfigureIoInterrupt(int irq, uint32 config) = 0;
	virtual void EndOfInterrupt(int irq) = 0;
	virtual int32 AssignToCpu(int32 irq, int32 cpu) = 0;
};


extern "C" {

// `_ex` functions must be used if using `generic_int`

status_t reserve_io_interrupt_vectors_ex(long count, long startVector,
	enum interrupt_type type, InterruptSource* source);
status_t allocate_io_interrupt_vectors_ex(long count, long *startVector,
	enum interrupt_type type, InterruptSource* source);
void free_io_interrupt_vectors_ex(long count, long startVector);

}

#endif

#endif	// _GENERIC_INT_H_
