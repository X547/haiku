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
	virtual void EnableIoInterrupt(int32 irq) = 0;
	virtual void DisableIoInterrupt(int32 irq) = 0;
	virtual void ConfigureIoInterrupt(int32 irq, uint32 config) = 0;
	virtual void EndOfInterrupt(int32 irq) = 0;
	virtual int32 AssignToCpu(int32 irq, int32 cpu) = 0;
};


extern "C" {

// `_ex` functions must be used if using `generic_int`

status_t reserve_io_interrupt_vectors_ex(int32 count, int32 startVector,
	enum interrupt_type type, InterruptSource* source);
status_t allocate_io_interrupt_vectors_ex(int32 count, int32 *startVector,
	enum interrupt_type type, InterruptSource* source);
void free_io_interrupt_vectors_ex(int32 count, int32 startVector);

}

#endif

#endif	// _GENERIC_INT_H_
