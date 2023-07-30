#pragma once

#include <dm2/device_manager.h>


class Ps2InterruptHandler {
public:
	virtual status_t InterruptReceived() = 0;

protected:
	~Ps2InterruptHandler() = default;
};


class Ps2Device {
public:
	virtual status_t SetupInterrupt(Ps2InterruptHandler* handler) = 0;
	virtual status_t Write(uint8 data) = 0;
	virtual status_t Read(uint8* data) = 0;

protected:
	~Ps2Device() = default;
};
