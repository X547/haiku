#pragma once

#include <dm2/bus/USB.h>

#include <AutoDeleterOS.h>
#include <util/AutoLock.h>


class UsbDevFsNode: public DevFsNode, public DevFsNodeHandle {
public:
	UsbDevFsNode(UsbDevice* device): fDevice(device) {}

	Capabilities GetCapabilities() const final {return {.control = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) final;
	status_t Close() final;
	status_t Control(uint32 op, void* buffer, size_t length) final;

private:
	const usb_configuration_info* GetConfiguration(uint32 configIndex, status_t *status);
	const usb_interface_info* GetInterface(uint32 configIndex, uint32 interfaceIndex, uint32 alternateIndex, status_t *status);
	static void Callback(void* cookie, status_t status, void *data, size_t actualLength);

private:
	UsbDevice*	fDevice;
	mutex		fLock = MUTEX_INITIALIZER("usb_raw device lock");

	SemDeleter	fNotify;
	status_t	fStatus {};
	size_t		fActualLength {};
};
