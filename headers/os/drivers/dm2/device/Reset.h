#pragma once

#include <dm2/device_manager.h>


class ResetDevice;


class ResetController {
public:
	static inline const char ifaceName[] = "reset";

	virtual ResetDevice* GetDevice(const uint8* optInfo, uint32 optInfoSize) = 0;
};


class ResetDevice {
public:
	virtual DeviceNode* OwnerNode() = 0;

	virtual bool IsAsserted() const = 0;
	virtual status_t SetAsserted(bool doAssert) = 0;

protected:
	~ResetDevice() = default;
};
