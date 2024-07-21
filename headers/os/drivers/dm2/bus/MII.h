#pragma once

#include <dm2/device_manager.h>


class MiiBus {
public:
	static inline const char ifaceName[] = "busses/mii/bus";

	virtual status_t Read(uint32 addr, uint32 reg) = 0;
	virtual status_t Write(uint32 addr, uint32 reg, uint16 value) = 0;

protected:
	~MiiBus() = default;
};


class MiiDevice {
public:
	static inline const char ifaceName[] = "busses/mii/device";

	virtual status_t Read(uint32 reg) = 0;
	virtual status_t Write(uint32 reg, uint16 value) = 0;

protected:
	~MiiDevice() = default;
};
