#pragma once

#include <arch/generic/generic_int.h>


class InterruptController: public InterruptSource {
public:
	virtual const char* Name() = 0;
	virtual bool IsSpuriousInterrupt(int num) = 0;
	virtual bool IsLevelTriggeredInterrupt(int num) = 0;
};


void arch_int_set_interrupt_controller(InterruptController &controller);
