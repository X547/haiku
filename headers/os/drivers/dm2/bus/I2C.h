#pragma once


#include <dm2/device_manager.h>


typedef uint16 i2c_addr;


struct i2c_chunk {
	uint8* buffer;
	uint32 length;
	bool isWrite;
};


#if 0
class I2cDevice {
public:
	static inline const char ifaceName[] = "bus_managers/i2c/device";

	virtual status_t ExecCommand(i2c_op op, const void *cmdBuffer, size_t cmdLength, void* dataBuffer, size_t dataLength) = 0;
	virtual status_t AcquireBus() = 0;
	virtual void ReleaseBus() = 0;

protected:
	~I2cDevice() = default;
};
#endif


class I2cBus {
public:
	static inline const char ifaceName[] = "bus_managers/i2c/bus";

	virtual status_t ExecCommand(i2c_addr address, const i2c_chunk* chunks, uint32 chunkCount) = 0;
	virtual status_t AcquireBus() = 0;
	virtual void ReleaseBus() = 0;

protected:
	~I2cBus() = default;
};


#if 0
class I2cController {
public:
	virtual status_t ExecCommand(i2c_op op, i2c_addr slaveAddress, const void *cmdBuffer, size_t cmdLength, void* dataBuffer, size_t dataLength) = 0;
	virtual status_t AcquireBus() = 0;
	virtual void ReleaseBus() = 0;

protected:
	~I2cController() = default;
};
#endif
