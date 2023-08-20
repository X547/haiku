#pragma once

#include <dm2/device_manager.h>


class InterruptControllerDevice {
public:
	static inline const char ifaceName[] = "interrupt_controller";

	virtual status_t GetVector(const uint8* optInfo, uint32 optInfoSize, long* vector) = 0;

protected:
	~InterruptControllerDevice() = default;
};
