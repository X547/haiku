#pragma once

#include <dm2/device_manager.h>


class ClockDevice;


class ClockController {
public:
	static inline const char ifaceName[] = "clock";

	virtual ClockDevice* GetDevice(uint32* optInfo, uint32 optInfoSize) = 0;
};


class ClockDevice {
public:
	virtual DeviceNode* OwnerNode() = 0;

	virtual bool IsEnabled() = 0;
	virtual status_t SetEnabled(bool doEnable) = 0;

	virtual int64 GetRate() = 0;
	virtual status_t SetRate(int64 rate /* Hz */) = 0;

	virtual ClockDevice* GetParent() = 0;
	virtual status_t SetParent(ClockDevice* parent) = 0;

protected:
	~ClockDevice() = default;
};
