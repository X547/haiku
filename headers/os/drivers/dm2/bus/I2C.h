#pragma once


#include <dm2/device_manager.h>


typedef uint16 i2c_addr;
typedef enum {
	I2C_OP_READ = 0,
	I2C_OP_READ_STOP = 1,
	I2C_OP_WRITE = 2,
	I2C_OP_WRITE_STOP = 3,
	I2C_OP_READ_BLOCK = 5,
	I2C_OP_WRITE_BLOCK = 7,
} i2c_op;


#define IS_READ_OP(op)	(((op) & I2C_OP_WRITE) == 0)
#define IS_WRITE_OP(op)	(((op) & I2C_OP_WRITE) != 0)
#define IS_STOP_OP(op)	(((op) & 1) != 0)
#define IS_BLOCK_OP(op)	(((op) & 4) != 0)


class I2cDevice {
public:
	static inline const char ifaceName[] = "bus_managers/i2c/device";

	virtual status_t ExecCommand(i2c_op op, const void *cmdBuffer, size_t cmdLength, void* dataBuffer, size_t dataLength) = 0;
	virtual status_t AcquireBus() = 0;
	virtual void ReleaseBus() = 0;

protected:
	~I2cDevice() = default;
};


class I2cBus {
public:
	static inline const char ifaceName[] = "bus_managers/i2c/bus";

	virtual status_t ExecCommand(i2c_op op, i2c_addr slaveAddress, const void *cmdBuffer, size_t cmdLength, void* dataBuffer, size_t dataLength) = 0;
	virtual status_t AcquireBus() = 0;
	virtual void ReleaseBus() = 0;

protected:
	~I2cBus() = default;
};


class I2cController {
public:
	virtual status_t ExecCommand(i2c_op op, i2c_addr slaveAddress, const void *cmdBuffer, size_t cmdLength, void* dataBuffer, size_t dataLength) = 0;
	virtual status_t AcquireBus() = 0;
	virtual void ReleaseBus() = 0;

protected:
	~I2cController() = default;
};
