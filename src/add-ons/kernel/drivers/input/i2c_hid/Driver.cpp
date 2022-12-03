/*
 * Copyright 2020, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2008-2011 Michael Lotz <mmlr@mlotz.ch>
 * Distributed under the terms of the MIT license.
 */


//!	Driver for I2C Human Interface Devices.


#include <device_manager.h>
#include <bus/FDT.h>
#include <i2c.h>

#include "DeviceList.h"
#include "Driver.h"
#include "HIDDevice.h"
#include "ProtocolHandler.h"

#include <AutoDeleterDrivers.h>
#include <lock.h>
#include <util/AutoLock.h>

#include <new>
#include <stdio.h>
#include <string.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


struct hid_driver_cookie {
	device_node*			node;
	HIDDevice*				hidDevice;
};

struct device_cookie {
	ProtocolHandler*	handler;
	uint32				cookie;
	hid_driver_cookie*	driver_cookie;
};


#define I2C_HID_DRIVER_NAME "drivers/input/i2c_hid/driver_v1"
#define I2C_HID_DEVICE_NAME "drivers/input/i2c_hid/device_v1"

/* Base Namespace devices are published to */
#define I2C_HID_BASENAME "input/i2c_hid/%d"

static device_manager_info *sDeviceManager;

DeviceList *gDeviceList = NULL;
static mutex sDriverLock;


// #pragma mark - driver hooks


static status_t
i2c_hid_init_device(void *driverCookie, void **cookie)
{
	*cookie = driverCookie;
	return B_OK;
}


static void
i2c_hid_uninit_device(void *cookie)
{
	(void)cookie;
}


static status_t
i2c_hid_open(void *initCookie, const char *path, int flags, void **_cookie)
{
	TRACE("open(%s, %" B_PRIu32 ", %p)\n", path, flags, _cookie);

	device_cookie *cookie = new(std::nothrow) device_cookie();
	if (cookie == NULL)
		return B_NO_MEMORY;
	cookie->driver_cookie = (hid_driver_cookie*)initCookie;

	MutexLocker locker(sDriverLock);

	ProtocolHandler *handler = (ProtocolHandler *)gDeviceList->FindDevice(path);
	TRACE("  path %s: handler %p\n", path, handler);

	cookie->handler = handler;
	cookie->cookie = 0;

	status_t result = handler == NULL ? B_ENTRY_NOT_FOUND : B_OK;
	if (result == B_OK)
		result = handler->Open(flags, &cookie->cookie);

	if (result != B_OK) {
		delete cookie;
		return result;
	}

	*_cookie = cookie;

	return B_OK;
}


static status_t
i2c_hid_read(void *_cookie, off_t position, void *buffer, size_t *numBytes)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("read(%p, %" B_PRIu64 ", %p, %p (%" B_PRIuSIZE ")\n", cookie, position, buffer, numBytes,
		numBytes != NULL ? *numBytes : 0);
	return cookie->handler->Read(&cookie->cookie, position, buffer, numBytes);
}


static status_t
i2c_hid_write(void *_cookie, off_t position, const void *buffer,
	size_t *numBytes)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("write(%p, %" B_PRIu64 ", %p, %p (%" B_PRIuSIZE ")\n", cookie, position, buffer, numBytes,
		numBytes != NULL ? *numBytes : 0);
	return cookie->handler->Write(&cookie->cookie, position, buffer, numBytes);
}


static status_t
i2c_hid_control(void *_cookie, uint32 op, void *buffer, size_t length)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("control(%p, %" B_PRIu32 ", %p, %" B_PRIuSIZE ")\n", cookie, op, buffer, length);
	return cookie->handler->Control(&cookie->cookie, op, buffer, length);
}


static status_t
i2c_hid_close(void *_cookie)
{
	device_cookie *cookie = (device_cookie *)_cookie;

	TRACE("close(%p)\n", cookie);
	return cookie->handler->Close(&cookie->cookie);
}


static status_t
i2c_hid_free(void *_cookie)
{
	device_cookie *cookie = (device_cookie *)_cookie;
	TRACE("free(%p)\n", cookie);

	mutex_lock(&sDriverLock);

	HIDDevice *device = cookie->handler->Device();
	if (device->IsOpen()) {
		// another handler of this device is still open so we can't free it
	} else if (device->IsRemoved()) {
		// the parent device is removed already and none of its handlers are
		// open anymore so we can free it here
		delete device;
	}

	mutex_unlock(&sDriverLock);

	delete cookie;
	return B_OK;
}


//	#pragma mark - driver module API


static float
i2c_hid_support(device_node *parent)
{
	CALLED();

	const char* bus;
	status_t status = sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	const char* compatible;
	status = sDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "hid-over-i2c") != 0)
		return 0.0f;

	return 1.0f;
}


static status_t
i2c_hid_register_device(device_node *node)
{
	CALLED();

	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, { .string = "I2C HID Device" }},
		{ NULL }
	};

	return sDeviceManager->register_node(node, I2C_HID_DRIVER_NAME, attrs,
		NULL, NULL);
}


static status_t
i2c_hid_init_driver(device_node *node, void **driverCookie)
{
	CALLED();

	i2c_bus_interface *i2cBus = NULL;
	i2c_bus i2cBusCookie = NULL;
	i2c_addr deviceAddress = 0;
	uint32 descriptorAddress = 0;

	DeviceNodePutter<&sDeviceManager> fdtI2cDevNode(sDeviceManager->get_parent_node(node));
	fdt_device_module_info *fdtI2cDevModule;
	fdt_device* fdtI2cDev;
	CHECK_RET(sDeviceManager->get_driver(fdtI2cDevNode.Get(), (driver_module_info**)&fdtI2cDevModule, (void**)&fdtI2cDev));
	TRACE("(1)\n");

	DeviceNodePutter<&sDeviceManager> fdtI2cBusNode(sDeviceManager->get_parent_node(fdtI2cDevNode.Get()));
	fdt_device_module_info *fdtI2cBusModule;
	fdt_device* fdtI2cBus;
	CHECK_RET(sDeviceManager->get_driver(fdtI2cBusNode.Get(), (driver_module_info**)&fdtI2cBusModule, (void**)&fdtI2cBus));
	TRACE("(2)\n");

#if 0
	uint64 deviceAddress64;
	if (!fdtI2cDevModule->get_reg(fdtI2cDev, 0, &deviceAddress64, NULL))
		return B_ERROR;
	TRACE("(3)\n");
	deviceAddress = (i2c_addr)deviceAddress64;
#endif
	{
		int attrLen;
		const void *attr = fdtI2cDevModule->get_prop(fdtI2cDev, "reg", &attrLen);
		if (attr == NULL || attrLen != 4)
			return B_ERROR;
		TRACE("(3)\n");
		deviceAddress = B_BENDIAN_TO_HOST_INT32(*(const uint32*)attr);
	}
	{
		int attrLen;
		const void *attr = fdtI2cDevModule->get_prop(fdtI2cDev, "hid-descr-addr", &attrLen);
		if (attr == NULL || attrLen != 4)
			return B_ERROR;
		TRACE("(4)\n");
		descriptorAddress = B_BENDIAN_TO_HOST_INT32(*(const uint32*)attr);
	}
	{
		device_attr attrs[] = {
			{ "device/driver", B_STRING_TYPE, {string: I2C_BUS_MODULE_NAME} },
			{}
		};
		device_node *i2cBusNode = NULL;
		CHECK_RET(sDeviceManager->find_child_node(fdtI2cBusNode.Get(), attrs, &i2cBusNode));
		TRACE("(5)\n");
		DeviceNodePutter<&sDeviceManager> i2cBusNodePutter(i2cBusNode);
		CHECK_RET(sDeviceManager->get_driver(i2cBusNode, (driver_module_info**)&i2cBus, (void**)&i2cBusCookie));
	}

	TRACE("(6)\n");
	hid_driver_cookie *device = (hid_driver_cookie *)calloc(1, sizeof(hid_driver_cookie));
	if (device == NULL)
		return B_NO_MEMORY;

	*driverCookie = device;

	device->node = node;

	mutex_lock(&sDriverLock);
	HIDDevice *hidDevice
		= new(std::nothrow) HIDDevice(descriptorAddress, i2cBus, i2cBusCookie, deviceAddress);

	if (hidDevice != NULL && hidDevice->InitCheck() == B_OK) {
		device->hidDevice = hidDevice;
	} else
		delete hidDevice;

	mutex_unlock(&sDriverLock);

	return device->hidDevice != NULL ? B_OK : B_IO_ERROR;
}


static void
i2c_hid_uninit_driver(void *driverCookie)
{
	CALLED();
	hid_driver_cookie *device = (hid_driver_cookie*)driverCookie;

	free(device);
}


static status_t
i2c_hid_register_child_devices(void *cookie)
{
	CALLED();
	hid_driver_cookie *device = (hid_driver_cookie*)cookie;
	HIDDevice* hidDevice = device->hidDevice;
	if (hidDevice == NULL)
		return B_OK;
	for (uint32 i = 0;; i++) {
		ProtocolHandler *handler = hidDevice->ProtocolHandlerAt(i);
		if (handler == NULL)
			break;

		// As devices can be un- and replugged at will, we cannot
		// simply rely on a device count. If there is just one
		// keyboard, this does not mean that it uses the 0 name.
		// There might have been two keyboards and the one using 0
		// might have been unplugged. So we just generate names
		// until we find one that is not currently in use.
		int32 index = 0;
		char pathBuffer[128];
		const char *basePath = handler->BasePath();
		while (true) {
			sprintf(pathBuffer, "%s%" B_PRId32, basePath, index++);
			if (gDeviceList->FindDevice(pathBuffer) == NULL) {
				// this name is still free, use it
				handler->SetPublishPath(strdup(pathBuffer));
				break;
			}
		}

		gDeviceList->AddDevice(handler->PublishPath(), handler);

		sDeviceManager->publish_device(device->node, pathBuffer,
			I2C_HID_DEVICE_NAME);
	}


/*	int pathID = sDeviceManager->create_id(I2C_HID_PATHID_GENERATOR);
	if (pathID < 0) {
		ERROR("register_child_devices: couldn't create a path_id\n");
		return B_ERROR;
	}*/
	return B_OK;
}


static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL) {
				return B_NO_MEMORY;
			}
			mutex_init(&sDriverLock, "i2c hid driver lock");

			return B_OK;
		case B_MODULE_UNINIT:
			delete gDeviceList;
			gDeviceList = NULL;
			mutex_destroy(&sDriverLock);
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


//	#pragma mark -


driver_module_info i2c_hid_driver_module = {
	{
		I2C_HID_DRIVER_NAME,
		0,
		&std_ops
	},

	i2c_hid_support,
	i2c_hid_register_device,
	i2c_hid_init_driver,
	i2c_hid_uninit_driver,
	i2c_hid_register_child_devices,
	NULL,	// rescan
	NULL,	// removed
};


struct device_module_info i2c_hid_device_module = {
	{
		I2C_HID_DEVICE_NAME,
		0,
		NULL
	},

	i2c_hid_init_device,
	i2c_hid_uninit_device,
	NULL,

	i2c_hid_open,
	i2c_hid_close,
	i2c_hid_free,
	i2c_hid_read,
	i2c_hid_write,
	NULL,
	i2c_hid_control,

	NULL,
	NULL
};


module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{}
};


module_info *modules[] = {
	(module_info *)&i2c_hid_driver_module,
	(module_info *)&i2c_hid_device_module,
	NULL
};
