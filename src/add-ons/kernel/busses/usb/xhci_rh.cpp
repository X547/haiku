/*
 * Copyright 2011, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 * 		Jian Chiang <j.jian.chiang@gmail.com>
 */


#include "xhci.h"

#include <string.h>
#include <algorithm>
#include <new>

#include <AutoDeleter.h>
#include <ScopeExit.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define USB_MODULE_NAME "xhci roothub"


static void
set_bit(uint8* bits, uint32 index)
{
	bits[index / 8] |= 1 << (index % 8);
}


static void
clear_bit(uint8* bits, uint32 index)
{
	bits[index / 8] &= ~(1 << (index % 8));
}


static bool
is_bit_set(const uint8* bits, uint32 index)
{
	return (bits[index / 8] & (1 << (index % 8))) != 0;
}


status_t
XHCIRootHub::Init(UsbBusManager *busManager)
{
	TRACE_ALWAYS("XHCIRootHub::Init(isUsb3: %d)\n", IsUsb3());

	for (uint32 i = 0; i < fPortCount; i++) {
		TRACE_ALWAYS("  port[%" B_PRIu32 "]: %" B_PRIu32 "\n", i, fPorts[i]);
	}

	if (fPortCount == 0)
		return B_OK;

	CHECK_RET(busManager->CreateDevice(fDevice, NULL, 0, busManager->ID(),
		1, IsUsb3() ? USB_SPEED_SUPERSPEED : USB_SPEED_HIGHSPEED, this));

	for (uint32 i = 0; i < fXhci->PortCount(); i++) {
		uint32 portNo = i + 1;
		usb_port_status portStatus;
		if (fXhci->GetPortStatus(fPorts[i], &portStatus) >= B_OK) {
			if (portStatus.change != 0) {
				fHasChangedPorts = true;
				set_bit(fChangedPorts, portNo);
			}
		}
	}

	return B_OK;
}


XHCIRootHub::~XHCIRootHub()
{
	fDevice->Free();
}


uint8
XHCIRootHub::AddPort(uint32 xhciPort)
{
	fPorts[fPortCount] = xhciPort;
	fPortCount++;
	return fPortCount;
}


status_t
XHCIRootHub::ProcessTransfer(UsbBusTransfer *transfer)
{
	TRACE("XHCIRootHub::ProcessTransfer(%p)\n", transfer);

	if (transfer->TransferPipe()->Type() == USB_PIPE_INTERRUPT) {
		TRACE_ALWAYS("XHCIRootHub::ProcessInterruptTransfer(%p)\n", transfer);
		MutexLocker lock(&fLock);
		if (fInterruptTransfer != NULL) {
			TRACE_ALWAYS("  B_BUSY\n");
			return B_BUSY;
		}

		fInterruptTransfer = transfer;
		TryCompleteInterruptTransfer(lock);
		return B_OK;
	}

	return B_ERROR;
}


void
XHCIRootHub::TryCompleteInterruptTransfer(MutexLocker& lock)
{
	if (fInterruptTransfer != NULL && fHasChangedPorts) {
		fHasChangedPorts = false;
		for (uint32 i = 0; i < fXhci->PortCount(); i++) {
			uint32 portNo = i + 1;
			if (is_bit_set(fChangedPorts, portNo)) {
				usb_port_status portStatus;
				if (fXhci->GetPortStatus(fPorts[i], &portStatus) < B_OK || portStatus.change == 0)
					clear_bit(fChangedPorts, portNo);
				else
					fHasChangedPorts = true;
			}
		}

		if (fHasChangedPorts) {
			size_t actualLength = std::min<size_t>((fXhci->PortCount() + 1 + 7) / 8, fInterruptTransfer->DataLength());
			memcpy(fInterruptTransfer->Data(), fChangedPorts, actualLength);

			UsbBusTransfer* transfer = fInterruptTransfer;
			fInterruptTransfer = NULL;
			lock.Unlock();

			transfer->Finished(B_OK, actualLength);
			transfer->Free();
		}
	}
}


void
XHCIRootHub::PortStatusChanged(uint32 portNo)
{
	TRACE_ALWAYS("port change detected, port: %" B_PRIu32 "\n", portNo);

	if (portNo >= USB_MAX_PORT_COUNT)
		return;

	MutexLocker lock(&fLock);

	fHasChangedPorts = true;

	set_bit(fChangedPorts, portNo);

	TryCompleteInterruptTransfer(lock);
}
