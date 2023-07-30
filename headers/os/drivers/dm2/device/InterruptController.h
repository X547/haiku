#pragma once

#include <dm2/device_manager.h>


class InterruptControllerDevice {
public:
	static inline const char ifaceName[] = "interrupt_controller";

	virtual status_t GetVector(uint64 irq, long* vector) = 0;

protected:
	~InterruptControllerDevice() = default;
};
