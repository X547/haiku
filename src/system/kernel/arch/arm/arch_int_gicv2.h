/*
 * Copyright 2021 Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef ARCH_ARM_GICV2_H
#define ARCH_ARM_GICV2_H

#include <SupportDefs.h>

#include <arch/generic/generic_int.h>

#include "soc.h"

class GICv2InterruptController : public InterruptController {
public:
	GICv2InterruptController(uint32_t gicd_regs = 0, uint32_t gicc_regs = 0);

	void EnableIoInterrupt(int irq) final;
	void DisableIoInterrupt(int irq) final;
	void ConfigureIoInterrupt(int irq, uint32 config) final;
	int32 AssignToCpu(int32 irq, int32 cpu) final;
	void HandleInterrupt() final;

private:
	volatile uint32_t *fGicdRegs;
	volatile uint32_t *fGiccRegs;
};

#endif /* ARCH_ARM_GICV2_H */
