/*
 * Copyright 2020-2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DRIVERS_BUS_FDT_H
#define _DRIVERS_BUS_FDT_H


#include <dm2/device_manager.h>


class FdtInterruptMap;


class FdtBus {
public:
	static inline const char ifaceName[] = "bus_managers/fdt/bus";

	virtual DeviceNode* NodeByPhandle(int phandle) = 0;

protected:
	~FdtBus() = default;
};


class FdtDevice {
public:
	static inline const char ifaceName[] = "bus_managers/fdt/device";

	virtual DeviceNode* GetBus() = 0;
	virtual const char* GetName() = 0;
	virtual const void* GetProp(const char* name, int* len) = 0;
	virtual bool GetReg(uint32 ord, uint64* regs, uint64* len) = 0;
	virtual bool GetInterrupt(uint32 ord, DeviceNode** interruptController, uint64* interrupt) = 0;
	virtual FdtInterruptMap* GetInterruptMap() = 0;

protected:
	~FdtDevice() = default;
};


class FdtInterruptMap {
public:
	virtual void Print() = 0;
	virtual uint32 Lookup(uint32 childAddr, uint32 childIrq) = 0;

protected:
	~FdtInterruptMap() = default;
};


/* Attributes of FDT device nodes */

#define B_FDT_DEVICE_NODE		"fdt/node"			/* uint32 */
#define B_FDT_DEVICE_NAME		"fdt/name"			/* string */
#define B_FDT_DEVICE_TYPE		"fdt/device_type"	/* string */
#define B_FDT_DEVICE_COMPATIBLE	"fdt/compatible"	/* string */


#endif // _DRIVERS_BUS_FDT_H
