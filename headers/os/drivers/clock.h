/*
 *	Copyright 2023, Haiku Inc. All Rights Reserved.
 *	Distributed under the terms of the MIT License.
 */

#ifndef DRIVERS_CLOCK_H
#define DRIVERS_CLOCK_H


#include <device_manager.h>


struct clock_device;


typedef struct clock_device_module_info {
	driver_module_info info;
	int32    (*is_enabled)  (struct clock_device* dev, int32 id);
	int32    (*set_enabled) (struct clock_device* dev, int32 id);
	int64    (*get_rate)    (struct clock_device* dev, int32 id);
	int64    (*set_rate)    (struct clock_device* dev, int32 id, int64 rate);
	int64    (*set_rate_dry)(struct clock_device* dev, int32 id, int64 rate);
	status_t (*get_parent)  (struct clock_device* dev, int32 id, struct clock_device** parentDev, int32* parentId);
	status_t (*set_parent)  (struct clock_device* dev, int32 id, struct clock_device*  parentDev, int32  parentId);
} clock_device_module_info;


#endif	/* DRIVERS_CLOCK_H */
