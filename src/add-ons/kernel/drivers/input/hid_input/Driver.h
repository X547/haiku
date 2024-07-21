/*
	Driver for I2C Human Interface Devices.
	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
	Distributed under the terms of the MIT license.
*/
#ifndef _HID_INPUT_DRIVER_H_
#define _HID_INPUT_DRIVER_H_

#include <Drivers.h>
#include <KernelExport.h>
#include <OS.h>
#include <util/kernel_cpp.h>

#include "DeviceList.h"

#define DRIVER_NAME	"hid_input"
#define DEVICE_PATH_SUFFIX	"hid"
#define DEVICE_NAME	"HID"

extern DeviceList *gDeviceList;


//#define TRACE_HID_INPUT
#ifdef TRACE_HID_INPUT
#	define TRACE(x...) dprintf(DRIVER_NAME ": " x)
#else
#	define TRACE(x...)
#endif
#define ERROR(x...) dprintf(DRIVER_NAME ": " x)
#define TRACE_ALWAYS(x...)	dprintf(DRIVER_NAME ": " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#endif //_HID_INPUT_DRIVER_H_
