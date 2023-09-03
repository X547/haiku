/*
 * Copyright 2003-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */

#include "usb_private.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


BusManager::BusManager(UsbHostController* hostCtrl, DeviceNode* node)
	:	fInitOK(false),
		fHostController(hostCtrl),
		fRootHub(NULL),
		fStackIndex((uint32)-1),
		fNode(node),
		fBusManagerIface(*this)
{
	mutex_init(&fLock, "usb busmanager lock");

	fRootObject = new(std::nothrow) Object(&Stack::Instance(), this);
	if (!fRootObject)
		return;

	// Clear the device map
	for (int32 i = 0; i < 128; i++)
		fDeviceMap[i] = false;
	fDeviceIndex = 0;

	// Set the default pipes to NULL (these will be created when needed)
	for (int32 i = 0; i <= USB_SPEED_MAX; i++)
		fDefaultPipes[i] = NULL;

	fInitOK = true;
}


BusManager::~BusManager()
{
	Lock();
	mutex_destroy(&fLock);
	for (int32 i = 0; i <= USB_SPEED_MAX; i++)
		delete fDefaultPipes[i];
	delete fRootObject;
}


status_t
BusManager::InitCheck()
{
	if (fInitOK)
		return B_OK;

	return B_ERROR;
}


bool
BusManager::Lock()
{
	return (mutex_lock(&fLock) == B_OK);
}


void
BusManager::Unlock()
{
	mutex_unlock(&fLock);
}


int8
BusManager::AllocateAddress()
{
	if (!Lock())
		return -1;

	int8 tries = 127;
	int8 address = fDeviceIndex;
	while (tries-- > 0) {
		if (fDeviceMap[address] == false) {
			fDeviceIndex = (address + 1) % 127;
			fDeviceMap[address] = true;
			Unlock();
			return address + 1;
		}

		address = (address + 1) % 127;
	}

	TRACE_ERROR("the busmanager has run out of device addresses\n");
	Unlock();
	return -1;
}


void
BusManager::FreeAddress(int8 address)
{
	address--;
	if (address < 0)
		return;

	if (!Lock())
		return;

	if (!fDeviceMap[address]) {
		TRACE_ERROR("freeing address %d which was not allocated\n", address);
	}

	fDeviceMap[address] = false;
	Unlock();
}


Device *
BusManager::AllocateDevice(Hub *parent, int8 hubAddress, uint8 hubPort,
	usb_speed speed)
{
	UsbBusDevice *deviceIface = fHostController->AllocateDevice(parent->GetBusDeviceIface(), hubAddress, hubPort, speed);
	if (deviceIface == NULL)
		return NULL;

	Device *device = static_cast<UsbBusDeviceImpl*>(deviceIface)->Base();
	device->RegisterNode();
	return device;
}


void
BusManager::FreeDevice(Device *device)
{
	return fHostController->FreeDevice(device->GetBusDeviceIface());
}


status_t
BusManager::Start()
{
	Stack::Instance().AddBusManager(this);
	fStackIndex = Stack::Instance().IndexOfBusManager(this);

	CHECK_RET(fHostController->Start());
	if (fRootHub != NULL) {
		fRootHub->RegisterNode(fNode);
	}

	Stack::Instance().Explore();
	return B_OK;
}


status_t
BusManager::Stop()
{
	// TODO: Stack::Instance().RemoveBusManager(this);
	return fHostController->Stop();
}


status_t
BusManager::StartDebugTransfer(Transfer *transfer)
{
	return fHostController->StartDebugTransfer(transfer->GetBusTransferIface());
}


status_t
BusManager::CheckDebugTransfer(Transfer *transfer)
{
	return fHostController->CheckDebugTransfer(transfer->GetBusTransferIface());
}


void
BusManager::CancelDebugTransfer(Transfer *transfer)
{
	fHostController->CancelDebugTransfer(transfer->GetBusTransferIface());
}


status_t
BusManager::SubmitTransfer(Transfer *transfer)
{
	return fHostController->SubmitTransfer(transfer->GetBusTransferIface());
}


status_t
BusManager::CancelQueuedTransfers(Pipe *pipe, bool force)
{
	return fHostController->CancelQueuedTransfers(pipe->GetBusPipeIface(), force);
}


status_t
BusManager::NotifyPipeChange(Pipe *pipe, usb_change change)
{
	return fHostController->NotifyPipeChange(pipe->GetBusPipeIface(), change);
}


ControlPipe *
BusManager::_GetDefaultPipe(usb_speed speed)
{
	if (!Lock())
		return NULL;

	if (fDefaultPipes[speed] == NULL) {
		fDefaultPipes[speed] = new(std::nothrow) ControlPipe(fRootObject);
		fDefaultPipes[speed]->InitCommon(0, 0, speed, Pipe::Default, 8, 0, 0, 0);
	}

	if (!fDefaultPipes[speed]) {
		TRACE_ERROR("failed to allocate default pipe for speed %d\n", speed);
	}

	Unlock();
	return fDefaultPipes[speed];
}

