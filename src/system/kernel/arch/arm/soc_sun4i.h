/*
 * Copyright 2022, Adrien Destugues, pulkomandy@pulkomandy.tk
 * Distributed under the terms of the MIT License.
 */

#ifndef ARCH_ARM_SOC_SUN4I_H
#define ARCH_ARM_SOC_SUN4I_H


#include "soc.h"


class Sun4iInterruptController : public InterruptController {
public:
	Sun4iInterruptController(uint32_t reg_base);

	void EnableIoInterrupt(int irq) final;
	void DisableIoInterrupt(int irq) final;
	void ConfigureIoInterrupt(int irq, uint32 config) final;
	int32 AssignToCpu(int32 irq, int32 cpu) final;
	void HandleInterrupt() final;

protected:
	area_id fRegArea;
	uint32 *fRegBase;
};


#endif /* !SOC_SUN4I_H */
