#pragma once

#include <dm2/device_manager.h>


class InterruptControllerDeviceFdt {
public:
	static inline const char ifaceName[] = "interrupt_controller/fdt";

	virtual status_t GetVector(const uint32* intrData, uint32 intrCells, long* vector) = 0;

protected:
	~InterruptControllerDeviceFdt() = default;
};
