#pragma once

#include <dm2/device_manager.h>


class SysconDevice {
public:
	static inline const char ifaceName[] = "syscon";

	virtual status_t Read4(uint32 offset, uint32 mask, uint32* value) = 0;
	virtual status_t Write4(uint32 offset, uint32 mask, uint32 value) = 0;

protected:
	~SysconDevice() = default;
};
