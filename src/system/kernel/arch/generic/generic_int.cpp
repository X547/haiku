/*
 * Copyright 2022, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <arch/generic/generic_int.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


static InterruptSource* sSources[NUM_IO_VECTORS];


void
arch_int_enable_io_interrupt(int irq)
{
	sSources[irq]->EnableIoInterrupt(irq);
}


void
arch_int_disable_io_interrupt(int irq)
{
	sSources[irq]->DisableIoInterrupt(irq);
}


void
arch_int_configure_io_interrupt(int irq, uint32 config)
{
	sSources[irq]->ConfigureIoInterrupt(irq, config);
}


void
arch_end_of_interrupt(int irq)
{
	sSources[irq]->EndOfInterrupt(irq);
}


int32
arch_int_assign_to_cpu(int32 irq, int32 cpu)
{
	return sSources[irq]->AssignToCpu(irq, cpu);
}


//#pragma mark - Kernel API

status_t
reserve_io_interrupt_vectors_ex(int32 count, int32 startVector,
	enum interrupt_type type, InterruptSource* source)
{
	CHECK_RET(reserve_io_interrupt_vectors(count, startVector, type));
	for (int32 i = 0; i < count; i++)
		sSources[startVector + i] = source;

	return B_OK;
}


status_t
allocate_io_interrupt_vectors_ex(int32 count, int32 *startVector,
	enum interrupt_type type, InterruptSource* source)
{
	CHECK_RET(allocate_io_interrupt_vectors(count, startVector, type));
	for (int32 i = 0; i < count; i++)
		sSources[*startVector + i] = source;

	return B_OK;
}


void
free_io_interrupt_vectors_ex(int32 count, int32 startVector)
{
	free_io_interrupt_vectors(count, startVector);
	for (int32 i = 0; i < count; i++)
		sSources[startVector + i] = NULL;
}
