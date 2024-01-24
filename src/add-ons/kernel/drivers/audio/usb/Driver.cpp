/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *
 */

#include <new>

#include <kernel.h>
#include <util/AutoLock.h>
#include <AutoDeleter.h>
#include <ContainerOf.h>

#include "Driver.h"
#include "Device.h"
#include "Settings.h"


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define USB_AUDIO_DRIVER_MODULE_NAME "drivers/audio/usb_audio/driver/v1"

#define DEVFS_PATH_FORMAT "audio/hmulti/usb/%" B_PRIu32


class UsbAudioDriver: public DeviceDriver {
public:
	UsbAudioDriver(DeviceNode* node, UsbDevice* usbDevice):
		fNode(node), fUsbDevice(usbDevice), fDevice(usbDevice) {}
	virtual ~UsbAudioDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	friend class UsbAudioDevFsNodeHandle;
	friend class UsbAudioDevFsNode;

	mutex fLock = MUTEX_INITIALIZER("UsbAudioDriver");

	DeviceNode* fNode;
	UsbDevice* fUsbDevice;
	Device fDevice;
	char fName[B_OS_NAME_LENGTH] {};

	class DevFsNode: public ::DevFsNode, public DevFsNodeHandle {
	public:
		UsbAudioDriver& Base() {return ContainerOf(*this, &UsbAudioDriver::fDevFsNode);}

		virtual ~DevFsNode() = default;

		Capabilities GetCapabilities() const final;
		status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;
		status_t Close() final;
		void Free() final;
		status_t Control(uint32 op, void *buffer, size_t length, bool isKernel) final;
	} fDevFsNode;
};


// #pragma mark - UsbAudioDriver

status_t
UsbAudioDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	UsbDevice* usbDevice = node->QueryBusInterface<UsbDevice>();

	ObjectDeleter<UsbAudioDriver> driver(new(std::nothrow) UsbAudioDriver(node, usbDevice));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
UsbAudioDriver::Init()
{
	CHECK_RET(fDevice.InitCheck());

	static int32 lastId = 0;
	int32 id = lastId++;
	sprintf(fName, DEVFS_PATH_FORMAT, id);

	CHECK_RET(fNode->RegisterDevFsNode(fName, &fDevFsNode));

	return B_OK;
}


// #pragma mark - UsbAudioDriver::DevFsNode

DevFsNode::Capabilities
UsbAudioDriver::DevFsNode::GetCapabilities() const
{
	return {
		.control = true
	};
}


status_t
UsbAudioDriver::DevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	MutexLocker locker(&Base().fLock);

	CHECK_RET(Base().fDevice.Open(openMode));

	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
UsbAudioDriver::DevFsNode::Close()
{
	MutexLocker locker(&Base().fLock);

	return Base().fDevice.Close();
}


void
UsbAudioDriver::DevFsNode::Free()
{
	Base().fDevice.Free();
}


status_t
UsbAudioDriver::DevFsNode::Control(uint32 op, void* arg, size_t length, bool isKernel)
{
	return Base().fDevice.Control(op, arg, length);
}


// #pragma mark -

static status_t
usb_audio_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			load_settings();
			TRACE(ERR, "%s\n", kVersion); // TODO: always???
			return B_OK;

		case B_MODULE_UNINIT:
			release_settings();
			return B_OK;

		default:
			return B_ERROR;
	}
}


static driver_module_info sUsbAudioDriverModule = {
	.info = {
		.name = USB_AUDIO_DRIVER_MODULE_NAME,
		.std_ops = usb_audio_std_ops
	},
	.probe = UsbAudioDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sUsbAudioDriverModule,
	NULL
};
