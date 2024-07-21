/*
 * Copyright 2022, Adrien Destugues, pulkomandy@pulkomandy.tk
 * Distributed under the terms of the MIT License.
 */

#include <vm/vm.h>

#include "soc_sun4i.h"


#define SUN4I_INTC_PEND_REG0	0x10
#define SUN4I_INTC_PEND_REG1	0x14
#define SUN4I_INTC_PEND_REG2	0x18
#define SUN4I_INTC_MASK_REG0	0x50
#define SUN4I_INTC_MASK_REG1	0x54
#define SUN4I_INTC_MASK_REG2	0x58


void
Sun4iInterruptController::EnableIoInterrupt(int32 irq)
{
	if (irq <= 31) {
		fRegBase[SUN4I_INTC_MASK_REG0] |= 1 << irq;
		return;
	}

	if (irq <= 63) {
		fRegBase[SUN4I_INTC_MASK_REG1] |= 1 << (irq - 32);
		return;
	}

	fRegBase[SUN4I_INTC_MASK_REG2] |= 1 << (irq - 64);
}


void
Sun4iInterruptController::DisableIoInterrupt(int32 irq)
{
	if (irq <= 31) {
		fRegBase[SUN4I_INTC_MASK_REG0] &= ~(1 << irq);
		return;
	}

	if (irq <= 63) {
		fRegBase[SUN4I_INTC_MASK_REG1] &= ~(1 << (irq - 32));
		return;
	}

	fRegBase[SUN4I_INTC_MASK_REG1] &= ~(1 << (irq - 64));
}


void
Sun4iInterruptController::HandleInterrupt()
{
	// FIXME can we use the hardware managed interrupt vector instead?
	for (int i=0; i < 32; i++) {
		if (fRegBase[SUN4I_INTC_PEND_REG0] & (1 << i))
			int_io_interrupt_handler(i, true);
	}

	for (int i=0; i < 32; i++) {
		if (fRegBase[SUN4I_INTC_PEND_REG1] & (1 << i))
			int_io_interrupt_handler(i + 32, true);
	}

	for (int i=0; i < 32; i++) {
		if (fRegBase[SUN4I_INTC_PEND_REG2] & (1 << i))
			int_io_interrupt_handler(i + 64, true);
	}
}


void
Sun4iInterruptController::ConfigureIoInterrupt(int32 irq, uint32 config)
{
}


int32
Sun4iInterruptController::AssignToCpu(int32 irq, int32 cpu)
{
	return 0;
}


Sun4iInterruptController::Sun4iInterruptController(uint32_t reg_base)
{
	fRegArea = vm_map_physical_memory(B_SYSTEM_TEAM, "intc-sun4i", (void**)&fRegBase,
		B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		reg_base, false);
	if (fRegArea < 0)
		panic("Sun4iInterruptController: cannot map registers!");

	reserve_io_interrupt_vectors_ex(96, 0, INTERRUPT_TYPE_IRQ, this);

	fRegBase[SUN4I_INTC_MASK_REG0] = 0;
	fRegBase[SUN4I_INTC_MASK_REG1] = 0;
	fRegBase[SUN4I_INTC_MASK_REG2] = 0;
}
