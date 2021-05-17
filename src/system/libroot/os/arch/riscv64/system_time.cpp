/*
 * Copyright 2012, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		François Revol <revol@free.fr>
 */

#include <OS.h>

#include <arch_cpu.h>
#include <libroot_private.h>
#include <real_time_data.h>


bigtime_t
system_time(void)
{
	// TODO: units conversion

	// Currently TinyEMU implementation just use host system system_time() to
	// implement `utime` register. Specification don't define units.

	// Spec says: The execution environment should provide a means of
	// determining the period of the real-time counter (seconds/tick).
	// The period must be constant.

	bigtime_t time;
	asm volatile("csrr %0, time" : "=r" (time));
	return time;
}

