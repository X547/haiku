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

	// Clear the device map
	for (int32 i = 0; i < 128; i++)
		fDeviceMap[i] = false;
	fDeviceIndex = 0;

	fInitOK = true;
}


BusManager::~BusManager()
{
	Lock();
	mutex_destroy(&fLock);
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
BusManager::AllocateDevice(Device *parent, int8 hubAddress, uint8 hubPort,
	usb_speed speed)
{
	UsbBusDevice *deviceIface = fHostController->AllocateDevice(
		parent->GetBusDeviceIface(), hubAddress, hubPort, speed);
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

	return B_OK;
}


status_t
BusManager::Stop()
{
	// TODO: Stack::Instance().RemoveBusManager(this);
	return fHostController->Stop();
}
