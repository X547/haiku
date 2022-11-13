/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#ifndef _INTERRUPT_CONTROLLER_H
#define _INTERRUPT_CONTROLLER_H

#include <device_manager.h>

typedef struct interrupt_controller_module_info {
	driver_module_info info;

	status_t (*get_vector)(void* cookie, uint64 irq, long* vector);
} interrupt_controller_module_info;

#endif	// _INTERRUPT_CONTROLLER_H
