/*
 * Copyright 2020-2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _DRIVERS_BUS_FDT_H
#define _DRIVERS_BUS_FDT_H


#include <dm2/device_manager.h>
#include <ByteOrder.h>


class ClockDevice;
class ResetDevice;
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
	virtual status_t GetRegByName(const char* name, uint64* regs, uint64* len) = 0;
	virtual bool GetInterrupt(uint32 ord, DeviceNode** interruptController, uint64* interrupt) = 0;
	virtual status_t GetInterruptByName(const char* name, DeviceNode** interruptController, uint64* interrupt) = 0;
	virtual FdtInterruptMap* GetInterruptMap() = 0;

	// TODO: declare dependency on clock controller driver
	virtual status_t GetClock(uint32 ord, ClockDevice** clock) = 0;
	virtual status_t GetClockByName(const char* name, ClockDevice** clock) = 0;
	virtual status_t GetReset(uint32 ord, ResetDevice** reset) = 0;
	virtual status_t GetResetByName(const char* name, ResetDevice** reset) = 0;

	inline status_t GetPropUint32(const char* name, uint32& val);
	inline status_t GetPropUint64(const char* name, uint64& val);

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


inline status_t
FdtDevice::GetPropUint32(const char* name, uint32& val)
{
	int propLen;
	const void* prop = GetProp(name, &propLen);
	if (prop == NULL)
		return B_NAME_NOT_FOUND;

	if (propLen != 4)
		return B_BAD_VALUE;

	val = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
	return B_OK;
}


inline status_t
FdtDevice::GetPropUint64(const char* name, uint64& val)
{
	int propLen;
	const void* prop = GetProp(name, &propLen);
	if (prop == NULL)
		return B_NAME_NOT_FOUND;

	if (propLen != 8)
		return B_BAD_VALUE;

	val = B_BENDIAN_TO_HOST_INT64(*(const uint32*)prop);
	return B_OK;
}


/* Attributes of FDT device nodes */

#define B_FDT_DEVICE_NODE		"fdt/node"			/* uint32 */
#define B_FDT_DEVICE_NAME		"fdt/name"			/* string */
#define B_FDT_DEVICE_TYPE		"fdt/device_type"	/* string */
#define B_FDT_DEVICE_COMPATIBLE	"fdt/compatible"	/* string */


#endif // _DRIVERS_BUS_FDT_H
