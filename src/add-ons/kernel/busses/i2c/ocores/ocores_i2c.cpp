/*
 * Copyright 2022-2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include "ocores_i2c.h"

#include <string.h>
#include <new>


status_t
OcoresI2cDriver::WaitCompletion()
{
	while (!fRegs->status.interrupt) {}
	return B_OK;
}


status_t
OcoresI2cDriver::WriteByte(OcoresI2cRegsCommand cmd, uint8 val)
{
	cmd.intAck = true;
	cmd.write = true;
	//dprintf("OcoresI2c::WriteByte(cmd: %#02x, val: %#02x)\n", cmd.val, val);
	fRegs->data = val;
	fRegs->command.val = cmd.val;
	CHECK_RET(WaitCompletion());
	return B_OK;
}


status_t
OcoresI2cDriver::ReadByte(OcoresI2cRegsCommand cmd, uint8& val)
{
	cmd.intAck = true;
	cmd.read = true;
	cmd.nack = cmd.stop;
	fRegs->command.val = cmd.val;
	CHECK_RET(WaitCompletion());
	val = fRegs->data;
	//dprintf("OcoresI2c::ReadByte(cmd: %#02x, val: %#02x)\n", cmd.val, val);
	return B_OK;
}


status_t
OcoresI2cDriver::WriteAddress(i2c_addr adr, bool isRead)
{
	// TODO: 10 bit address support
	//dprintf("OcoresI2cDriver::WriteAddress(adr: %#04x, isRead: %d)\n", adr, isRead);
	uint8 val = OcoresI2cRegsAddress7{.read = isRead, .address = (uint8)adr}.val;
	CHECK_RET(WriteByte({.start = true}, val));
	return B_OK;
}


status_t
OcoresI2cDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<OcoresI2cDriver> driver(new(std::nothrow) OcoresI2cDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
OcoresI2cDriver::Init()
{
	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fFdtDevice->GetReg(0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("Ocores i2c MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	uint64 irq;
	if (!fFdtDevice->GetInterrupt(0, NULL, &irq))
		return B_ERROR;
	fIrqVector = irq; // TODO: take interrupt controller into account

	return B_OK;
}


void*
OcoresI2cDriver::QueryInterface(const char* name)
{
	if (strcmp(name, I2cBus::ifaceName) == 0)
		return static_cast<I2cBus*>(this);

	return NULL;
}


status_t
OcoresI2cDriver::ExecCommand(i2c_op op, i2c_addr slaveAddress, const uint8 *cmdBuffer, size_t cmdLength, uint8* dataBuffer, size_t dataLength)
{
	//dprintf("OcoresI2cDriver::ExecCommand()\n");
	if (cmdLength > 0) {
		CHECK_RET(WriteAddress(slaveAddress, false));
		do {
			if (fRegs->status.nackReceived) {
				fRegs->command.val = OcoresI2cRegsCommand{
					.intAck = true,
					.stop = true
				}.val;
				return B_ERROR;
			}
			cmdLength--;
			CHECK_RET(WriteByte({.stop = IS_STOP_OP(op) && cmdLength == 0 && dataLength == 0}, *cmdBuffer++));
		} while (cmdLength > 0);
	}
	if (dataLength > 0) {
		CHECK_RET(WriteAddress(slaveAddress, true));
		do {
			dataLength--;
			CHECK_RET(ReadByte({.stop = IS_STOP_OP(op) && dataLength == 0}, *dataBuffer++));
		} while (dataLength > 0);
	}
	return B_OK;
}


status_t
OcoresI2cDriver::AcquireBus()
{
	return mutex_lock(&fLock);
}


void
OcoresI2cDriver::ReleaseBus()
{
	mutex_unlock(&fLock);
}



static driver_module_info sOcoresI2cDriverModule = {
	.info = {
		.name = OCORES_I2C_DRIVER_MODULE_NAME,
	},
	.probe = OcoresI2cDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sOcoresI2cDriverModule,
	NULL
};
