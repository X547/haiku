/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011, Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for I2C Human Interface Devices.


#include "Driver.h"
#include "HIDDevice.h"
#include "HIDReport.h"
#include "HIDWriter.h"
#include "ProtocolHandler.h"

#include <usb/USB_hid.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <new>


HIDDevice::HIDDevice(uint16 descriptorAddress, i2c_bus_interface* i2cBus,
	i2c_bus i2cBusCookie, i2c_addr address, long irqVector)
	:	fStatus(B_NO_INIT),
		fTransferLastschedule(0),
		fTransferScheduled(0),
		fTransferBufferSize(0),
		fTransferBuffer(NULL),
		fOpenCount(0),
		fRemoved(false),
		fParser(this),
		fProtocolHandlerCount(0),
		fProtocolHandlerList(NULL),
		fDescriptorAddress(descriptorAddress),
		fI2cBus(i2cBus),
		fI2cBusCookie(i2cBusCookie),
		fDeviceAddress(address),
		fIrqVector(irqVector)
{
	// fetch HID descriptor
	fStatus = _FetchBuffer((uint8*)&fDescriptorAddress,
		sizeof(fDescriptorAddress), &fDescriptor, sizeof(fDescriptor));
	if (fStatus != B_OK) {
		ERROR("failed to fetch HID descriptor\n");
		return;
	}

	// fetch HID Report descriptor

	HIDWriter descriptorWriter;

	uint16 descriptorLength = fDescriptor.wReportDescLength;
	fReportDescriptor = (uint8 *)malloc(descriptorLength);
	if (fReportDescriptor == NULL) {
		ERROR("failed to allocate buffer for report descriptor\n");
		fStatus = B_NO_MEMORY;
		return;
	}

	uint16 reportDescRegister = fDescriptor.wReportDescRegister;
	fStatus = _FetchBuffer((uint8*)&reportDescRegister,
		sizeof(reportDescRegister), fReportDescriptor,
		descriptorLength);
	if (fStatus != B_OK) {
		ERROR("failed tot get report descriptor\n");
		free(fReportDescriptor);
		return;
	}

#if 1
	// save report descriptor for troubleshooting
	char outputFile[128];
	sprintf(outputFile, "/tmp/i2c_hid_report_descriptor_%04x_%04x.bin",
		fDescriptor.wVendorID, fDescriptor.wProductID);
	int fd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		write(fd, fReportDescriptor, descriptorLength);
		close(fd);
	}
#endif

	status_t result = fParser.ParseReportDescriptor(fReportDescriptor,
		descriptorLength);
	free(fReportDescriptor);

	if (result != B_OK) {
		ERROR("parsing the report descriptor failed\n");
		fStatus = result;
		return;
	}

#if 0
	for (uint32 i = 0; i < fParser.CountReports(HID_REPORT_TYPE_ANY); i++)
		fParser.ReportAt(HID_REPORT_TYPE_ANY, i)->PrintToStream();
#endif

	fTransferBufferSize = fParser.MaxReportSize();
	if (fTransferBufferSize == 0) {
		TRACE_ALWAYS("report claims a report size of 0\n");
		return;
	}

	// Extra 2 bytes for buffer size header.
	fTransferBuffer = (uint8 *)malloc(fTransferBufferSize + 2);
	if (fTransferBuffer == NULL) {
		TRACE_ALWAYS("failed to allocate transfer buffer\n");
		fStatus = B_NO_MEMORY;
		return;
	}

	install_io_interrupt_handler(fIrqVector, _InterruptReceived, this, 0);

	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList,
		fProtocolHandlerCount);
	fStatus = B_OK;
}


HIDDevice::~HIDDevice()
{
	DPCQueue::DefaultQueue(B_URGENT_DISPLAY_PRIORITY)->Cancel(this);
	remove_io_interrupt_handler(fIrqVector, _InterruptReceived, this);

	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		ProtocolHandler *next = handler->NextHandler();
		delete handler;
		handler = next;
	}

	free(fTransferBuffer);
}


status_t
HIDDevice::Open(ProtocolHandler *handler, uint32 flags)
{
	atomic_add(&fOpenCount, 1);
	_Reset();

	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler *handler)
{
	atomic_add(&fOpenCount, -1);
	_SetPower(I2C_HID_POWER_OFF);

	return B_OK;
}


void
HIDDevice::Removed()
{
	fRemoved = true;
}


status_t
HIDDevice::MaybeScheduleTransfer(HIDReport *report)
{
	if (fRemoved)
		return ENODEV;

	return B_OK;
}


status_t
HIDDevice::SendReport(HIDReport *report)
{
	// TODO
	return B_OK;
}


ProtocolHandler *
HIDDevice::ProtocolHandlerAt(uint32 index) const
{
	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		if (index == 0)
			return handler;

		handler = handler->NextHandler();
		index--;
	}

	return NULL;
}


status_t
HIDDevice::_Reset()
{
	CALLED();
	status_t status = _SetPower(I2C_HID_POWER_ON);
	if (status != B_OK)
		return status;

	snooze(1000);

	uint8 cmd[] = {
		(uint8)(fDescriptor.wCommandRegister & 0xff),
		(uint8)(fDescriptor.wCommandRegister >> 8),
		0,
		I2C_HID_CMD_RESET,
	};

	status = _ExecCommand(I2C_OP_WRITE_STOP, cmd, sizeof(cmd), NULL, 0);
	if (status != B_OK) {
		_SetPower(I2C_HID_POWER_OFF);
		return status;
	}

	snooze(1000);
	return B_OK;
}


status_t
HIDDevice::_SetPower(uint8 power)
{
	CALLED();
	uint8 cmd[] = {
		(uint8)(fDescriptor.wCommandRegister & 0xff),
		(uint8)(fDescriptor.wCommandRegister >> 8),
		power,
		I2C_HID_CMD_SET_POWER
	};

	return _ExecCommand(I2C_OP_WRITE_STOP, cmd, sizeof(cmd), NULL, 0);
}


status_t
HIDDevice::_FetchBuffer(uint8* cmd, size_t cmdLength, void* buffer,
	size_t bufferLength)
{
	return _ExecCommand(I2C_OP_READ_STOP, cmd, cmdLength,
		buffer, bufferLength);
}


status_t
HIDDevice::_ExecCommand(i2c_op op, uint8* cmd, size_t cmdLength, void* buffer,
	size_t bufferLength)
{
	status_t status = fI2cBus->acquire_bus(fI2cBusCookie);
	if (status != B_OK)
		return status;
	status = fI2cBus->exec_command(fI2cBusCookie, op, fDeviceAddress, cmd, cmdLength,
		buffer, bufferLength);
	fI2cBus->release_bus(fI2cBusCookie);
	return status;
}


int32
HIDDevice::_InterruptReceived(void* arg)
{
	return static_cast<HIDDevice*>(arg)->_InterruptReceivedInt();
}


int32
HIDDevice::_InterruptReceivedInt()
{
	if (atomic_get_and_set(&fDpcQueued, 1) == 0)
		DPCQueue::DefaultQueue(B_URGENT_DISPLAY_PRIORITY)->Add(this);

	return B_HANDLED_INTERRUPT;
}


void
HIDDevice::DoDPC(DPCQueue* queue)
{
	atomic_set(&fDpcQueued, 0);

	status_t status = _FetchBuffer(NULL, 0, fTransferBuffer, fTransferBufferSize + 2);
	if (status != B_OK)
		return;

	uint16 actualLength = fTransferBuffer[0] | (fTransferBuffer[1] << 8);
#if 0
	if (actualLength == 0) {
		// TODO: handle reset
	}
#endif
	if (actualLength <= 2)
		actualLength = 0;
	else
		actualLength -= 2;

	fParser.SetReport(status, (uint8*)((addr_t)fTransferBuffer + 2), actualLength);
}
