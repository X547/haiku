#pragma once

#include <dm2/device_manager.h>


class ResetDevice;


class ResetController {
public:
	static inline const char ifaceName[] = "reset";

	virtual ResetDevice* GetDevice(uint32* optInfo, uint32 optInfoSize) = 0;
};


class ResetDevice {
public:
	virtual DeviceNode* OwnerNode() = 0;

	virtual bool IsAsserted(uint32 id) = 0;
	virtual status_t SetAsserted(uint32 id, bool doAssert) = 0;

protected:
	~ResetDevice() = default;
};
