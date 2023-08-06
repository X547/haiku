/*
 * Copyright 2022-2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _OCORES_I2C_H_
#define _OCORES_I2C_H_

#include <ByteOrder.h>
#include <assert.h>

#include <dm2/bus/I2C.h>
#include <dm2/bus/FDT.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <lock.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define OCORES_I2C_DRIVER_MODULE_NAME "busses/i2c/ocores_i2c/driver/v1"


static_assert(B_HOST_IS_LENDIAN);

union OcoresI2cRegsAddress7 {
	struct {
		uint8 read: 1;
		uint8 address: 7;
	};
	uint8 val;
};

union OcoresI2cRegsControl {
	struct {
		uint8 unused1: 6;
		uint8 intEnabled: 1;
		uint8 enabled: 1;
	};
	uint8 val;
};

union OcoresI2cRegsCommand {
	struct {
		uint8 intAck: 1;
		uint8 unused1: 2;
		uint8 nack: 1;
		uint8 write: 1;
		uint8 read: 1;
		uint8 stop: 1;
		uint8 start: 1;
	};
	uint8 val;
};

union OcoresI2cRegsStatus {
	struct {
		uint8 interrupt: 1;
		uint8 transferInProgress: 1;
		uint8 reserved1: 3;
		uint8 arbitrationLost: 1;
		uint8 busy: 1;
		uint8 nackReceived: 1;
	};
	uint8 val;
};

struct OcoresI2cRegs {
	uint8 preLo;
	uint8 align1[3];
	uint8 preHi;
	uint8 align2[3];
	OcoresI2cRegsControl control;
	uint8 align3[3];
	uint8 data;
	uint8 align4[3];
	union {
		OcoresI2cRegsCommand command;
		OcoresI2cRegsStatus status;
	};
	uint8 align5[3];
};


class OcoresI2cDriver: public DeviceDriver, public I2cBus {
public:
	OcoresI2cDriver(DeviceNode* node): fNode(node) {}
	virtual ~OcoresI2cDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// I2cBus
	status_t ExecCommand(i2c_op op, i2c_addr slaveAddress, const uint8 *cmdBuffer, size_t cmdLength, uint8* dataBuffer, size_t dataLength) final;
	status_t AcquireBus() final;
	void ReleaseBus() final;

private:
	status_t Init();

	status_t WaitCompletion();
	status_t WriteByte(OcoresI2cRegsCommand cmd, uint8 val);
	status_t ReadByte(OcoresI2cRegsCommand cmd, uint8& val);
	status_t WriteAddress(i2c_addr address, bool isRead);

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	volatile OcoresI2cRegs* fRegs{};
	long fIrqVector = -1;

	struct mutex fLock = MUTEX_INITIALIZER("Opencores i2c");
};


#endif	// _OCORES_I2C_H_
