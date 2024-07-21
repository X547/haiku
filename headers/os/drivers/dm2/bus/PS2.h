#pragma once

#include <dm2/device_manager.h>


#define PS2_DEVICE_ID	"ps2/id"	/* uint32 */


class Ps2DeviceCallback {
public:
	virtual void InputAvailable() = 0;

protected:
	~Ps2DeviceCallback() = default;
};


class Ps2Device {
public:
	static inline const char ifaceName[] = "bus_managers/ps2/device";

	virtual status_t SetCallback(Ps2DeviceCallback* callback) = 0;
	virtual status_t Read(uint8* data) = 0;
	virtual status_t Write(uint8 data) = 0;

protected:
	~Ps2Device() = default;
};
