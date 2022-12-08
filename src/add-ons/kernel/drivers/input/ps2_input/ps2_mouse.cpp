/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <DPC.h>
#include "ps2_input.h"

#include <input/keyboard_mouse_driver.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDrivers.h>
#include <util/AutoLock.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class Ps2Mouse {
private:
	friend class Ps2MouseDeviceCookie;
	
	enum {
		eventQueueLen = 256,
	};

	struct mutex fLock = MUTEX_INITIALIZER("PS/2 Mouse");

	ps2_device_interface* fDeviceModule{};
	ps2_device* fDevice{};
	device_node *fNode{};

	ConditionVariable fReadCondition{};
	spinlock fEventQueueLock = B_SPINLOCK_INITIALIZER;
	mouse_movement fEvents[eventQueueLen];
	uint32 fEventHead{}, fEventTail{};

	PortDeleter fPort;

	bigtime_t fClickLastTime{};
	bigtime_t fClick_speed = 500000;
	int fClickCount{};
	int fButtonsState{};

	class Callback: public DPCCallback {
	private:
		Ps2Mouse* drv;

	public:
		bool installed = false;

		Callback(Ps2Mouse* drv): drv(drv) {}
		virtual ~Callback() = default;

		void DoDPC(DPCQueue* queue) final
		{
			int32 avail;
			uint8 val;
			avail = drv->fDeviceModule->read(drv->fDevice, &val);
			while (avail >= 3) {
				uint8 packet[3];
				for (int32 i = 0; i < 3; i++) {
					packet[i] = val;
					drv->fDeviceModule->read(drv->fDevice, &val);
					avail--;
				}
				drv->EnqueuePacket(packet);
			}
			while (avail > 0)
				drv->fDeviceModule->read(drv->fDevice, &val);

			installed = false;
		}
	} fCallback;

public:
	Ps2Mouse(): fCallback(this) {}

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	status_t InitDriver(device_node* node);
	void UninitDriver();
	status_t RegisterChildDevices();

	static status_t HandleInterrupt(void* arg);

	void EnqueuePacket(uint8 packet[]);
};

class Ps2MouseDeviceCookie {
private:
	Ps2Mouse *fDriver;

public:
	status_t Open(Ps2Mouse* driver, const char *path, int openMode);
	status_t Close();
	status_t Free();
	status_t Read(off_t pos, void *buffer, size_t &length);
	status_t Write(off_t pos, const void *buffer, size_t &length);
	status_t Control(uint32 op, void *buffer, size_t length);
};


float
Ps2Mouse::SupportsDevice(device_node* parent)
{
	const char* bus;
	status_t status = gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "ps2") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
Ps2Mouse::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "PS/2 Mouse"} },
		{}
	};

	return gDeviceManager->register_node(parent, PS2_MOUSE_MODULE_NAME, attrs, NULL, NULL);
}


status_t
Ps2Mouse::InitDriver(device_node* node)
{
	dprintf("Ps2Mouse::InitDriver\n");
	fNode = node;

	DeviceNodePutter<&gDeviceManager> parent(gDeviceManager->get_parent_node(node));
	const char* bus;
	CHECK_RET(gDeviceManager->get_attr_string(parent.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "ps2") != 0)
		return B_ERROR;
	CHECK_RET(gDeviceManager->get_driver(parent.Get(), (driver_module_info**)&fDeviceModule, (void**)&fDevice));

	fDeviceModule->set_interrupt_handler(fDevice, HandleInterrupt, this);

	fReadCondition.Init(this, "event read");
	fPort.SetTo(create_port(128, "mouse events"));

	dprintf(" -> OK\n");
	return B_OK;
}


void
Ps2Mouse::UninitDriver()
{
	fDeviceModule->set_interrupt_handler(fDevice, NULL, NULL);
	delete this;
}


status_t
Ps2Mouse::RegisterChildDevices()
{
	int32 id = gDeviceManager->create_id("input/mouse/ps2");
	char path[256];
	sprintf(path, "input/mouse/ps2/%" B_PRId32, id);

	CHECK_RET(gDeviceManager->publish_device(fNode, path, PS2_MOUSE_DEVICE_MODULE_NAME));
	return B_OK;
}


int32
Ps2Mouse::HandleInterrupt(void* arg)
{
	//dprintf("Ps2Mouse::HandleInterrupt\n");
	Ps2Mouse* drv = (Ps2Mouse*)arg;

	if (!drv->fCallback.installed) {
		drv->fCallback.installed = true;
		DPCQueue::DefaultQueue(B_URGENT_DISPLAY_PRIORITY)->Add(&drv->fCallback);
	}

	return B_HANDLED_INTERRUPT;
}


void
Ps2Mouse::EnqueuePacket(uint8 packet[])
{
//	SpinLocker locker(&fEventQueueLock);

//	if (fEventTail - fEventHead >= eventQueueLen)
//		return;

//	bool wasEmpty = fEventHead == fEventTail;

	int buttons = packet[0] & 7;
	int xDelta = ((packet[0] & 0x10) ? ~0xff : 0) | packet[1];
	int yDelta = ((packet[0] & 0x20) ? ~0xff : 0) | packet[2];
	int xDeltaWheel = 0;
	int yDeltaWheel = 0;
	bigtime_t currentTime = system_time();
	
	if (buttons != 0 && fButtonsState == 0) {
		if (fClickLastTime + fClick_speed > currentTime)
			fClickCount++;
		else
			fClickCount = 1;

		fClickLastTime = currentTime;
	}

	fButtonsState = buttons;
/*
	if (cookie->flags & F_MOUSE_TYPE_INTELLIMOUSE) {
		yDeltaWheel = packet[3] & 0x07;
 		if (packet[3] & 0x08)
			yDeltaWheel |= ~0x07;
	}
*/

	//mouse_movement& pos = &fEvents[(fEventTail++) % eventQueueLen];
	mouse_movement pos{};

	pos.xdelta = xDelta;
	pos.ydelta = yDelta;
	pos.buttons = buttons;
	pos.clicks = fClickCount;
	pos.modifiers = 0;
	pos.timestamp = currentTime;
	pos.wheel_ydelta = yDeltaWheel;
	pos.wheel_xdelta = xDeltaWheel;

	write_port_etc(fPort.Get(), 1, &pos, sizeof(pos), B_RELATIVE_TIMEOUT, 0);
/*
	dprintf("ps2: ps2_packet_to_movement xdelta: %d, ydelta: %d, buttons %x, "
		"clicks: %d, timestamp %" B_PRIdBIGTIME "\n",
		xDelta, yDelta, buttons, fClickCount, currentTime);
*/
//	if (wasEmpty)
//		fReadCondition.NotifyOne();
}


status_t
Ps2MouseDeviceCookie::Open(Ps2Mouse* driver, const char *path, int openMode)
{
	fDriver = driver;
	return B_OK;
}


status_t
Ps2MouseDeviceCookie::Close()
{
	return B_OK;
}


status_t
Ps2MouseDeviceCookie::Free()
{
	delete this;
	return B_OK;
}


status_t
Ps2MouseDeviceCookie::Read(off_t pos, void *buffer, size_t &length)
{
	length = 0;
	return B_NOT_ALLOWED;
}


status_t
Ps2MouseDeviceCookie::Write(off_t pos, const void *buffer, size_t &length)
{
	length = 0;
	return B_NOT_ALLOWED;
}


status_t
Ps2MouseDeviceCookie::Control(uint32 op, void *buffer, size_t length)
{
	switch (op) {
		case MS_NUM_EVENTS: {
			//dprintf("MS_NUM_EVENTS\n");
			return port_count(fDriver->fPort.Get());
			//InterruptsSpinLocker spinlocker(&fDriver->fEventQueueLock);
			//return fDriver->fEventTail - fDriver->fEventHead;
		}
		case MS_READ: {
			//dprintf("+MS_READ\n");
/*
			InterruptsSpinLocker spinlocker(&fDriver->fEventQueueLock);

			while (fDriver->fEventTail == fDriver->fEventHead) {
				spinlocker.Unlock();
				CHECK_RET(fDriver->fReadCondition.Wait());
				spinlocker.Lock();
			}

			mouse_movement &movement = fDriver->fEvents[(fDriver->fEventHead++) % Ps2Mouse::eventQueueLen];
			//dprintf("-MS_READ\n");
*/
			int32 what;
			//mouse_movement movement;
			CHECK_RET(read_port(fDriver->fPort.Get(), &what, buffer, sizeof(mouse_movement)));
			//return user_memcpy(buffer, &movement, sizeof(movement));
			return B_OK;
		}
		case MS_SET_TYPE:
			return B_BAD_VALUE;
		case MS_GET_ACCEL:
			return B_BAD_VALUE;
		case MS_SET_ACCEL:
			return B_BAD_VALUE;
		case MS_SET_CLICKSPEED:
			return B_BAD_VALUE;
	}
	return B_DEV_INVALID_IOCTL;
}


driver_module_info gMouseModuleInfo = {
	{
		.name = PS2_MOUSE_MODULE_NAME,
	},
	.supports_device = [](device_node* parent) {
		return Ps2Mouse::SupportsDevice(parent);
	},
	.register_device = [](device_node* parent) {
		return Ps2Mouse::RegisterDevice(parent);
	},
	.init_driver = [](device_node* node, void** driverCookie) {
		ObjectDeleter<Ps2Mouse> driver(new(std::nothrow) Ps2Mouse());
		if (!driver.IsSet()) return B_NO_MEMORY;
		CHECK_RET(driver->InitDriver(node));
		*driverCookie = driver.Detach();
		return B_OK;
	},
	.uninit_driver = [](void* driverCookie) {
		return static_cast<Ps2Mouse*>(driverCookie)->UninitDriver();
	},
	.register_child_devices = [](void* driverCookie) {
		return static_cast<Ps2Mouse*>(driverCookie)->RegisterChildDevices();
	},
};

device_module_info gMouseDeviceModuleInfo = {
	{
		.name = PS2_MOUSE_DEVICE_MODULE_NAME,
	},
	.init_device = [](void *driverCookie, void **deviceCookie) {
		*deviceCookie = driverCookie;
		return B_OK;
	},
	.uninit_device = [](void *deviceCookie) {
	},
	.open = [](void *deviceCookie, const char *path, int openMode, void **cookie) {
		ObjectDeleter<Ps2MouseDeviceCookie> devCookie(new(std::nothrow) Ps2MouseDeviceCookie());
		if (!devCookie.IsSet()) return B_NO_MEMORY;
		CHECK_RET(devCookie->Open(static_cast<Ps2Mouse*>(deviceCookie), path, openMode));
		*cookie = devCookie.Detach();
		return B_OK;
	},
	.close = [](void *cookie) {
		return static_cast<Ps2MouseDeviceCookie*>(cookie)->Close();
	},
	.free = [](void *cookie) {
		return static_cast<Ps2MouseDeviceCookie*>(cookie)->Free();
	},
	.read = [](void *cookie, off_t pos, void *buffer, size_t *length) {
		return static_cast<Ps2MouseDeviceCookie*>(cookie)->Read(pos, buffer, *length);
	},
	.write = [](void *cookie, off_t pos, const void *buffer, size_t *length) {
		return static_cast<Ps2MouseDeviceCookie*>(cookie)->Write(pos, buffer, *length);
	},
	.control = [](void *cookie, uint32 op, void *buffer, size_t length) {
		return static_cast<Ps2MouseDeviceCookie*>(cookie)->Control(op, buffer, length);
	},
};
