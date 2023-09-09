/*
 * Copyright 2003-2023, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */


#include <stdio.h>

#include <algorithm>

#include <dm2/bus/USB.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <DPC.h>

#include "usbspec_private.h"


#define TRACE_USB
#ifdef TRACE_USB
#define TRACE(x...)			dprintf("usb hub: " x)
#else
#define TRACE(x...)			;
#endif

#define TRACE_ALWAYS(x...)	dprintf("usb hub: " x)
#define TRACE_ERROR(x...)	dprintf("[!] usb hub: " x)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define CHECK_RET_MSG(err, msg...) \
	{ \
		status_t _err = (err); \
		if (_err < B_OK) { \
			dprintf(msg); \
			return _err; \
		} \
	} \


#define USB_HUB_DRIVER_MODULE_NAME "bus_managers/usb/hub/driver/v1"


class UsbHubDriver: public DeviceDriver, private DPCCallback {
public:
	UsbHubDriver(DeviceNode* node): fNode(node) {}
	virtual ~UsbHubDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

	status_t UpdatePortStatus(uint8 index);
	status_t ResetPort(uint8 index);
	status_t DisablePort(uint8 index);
	status_t DebouncePort(uint8 index);
	void UpdatePort(uint8 index);

	static void InterruptCallback(void* cookie, status_t status, void* data, size_t actualLength);

	// DPCCallback
	void DoDPC(DPCQueue* queue) final;

private:
	DeviceNode* fNode;
	UsbDevice* fUsbDevice {};

	UsbPipe* fInterruptPipe {};
	usb_hub_descriptor fHubDescriptor {};

	usb_port_status fInterruptStatus[USB_MAX_PORT_COUNT] {};
	usb_port_status fPortStatus[USB_MAX_PORT_COUNT] {};
	UsbDevice* fChildren[USB_MAX_PORT_COUNT] {};
};


status_t
UsbHubDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<UsbHubDriver> driver(new(std::nothrow) UsbHubDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
UsbHubDriver::Init()
{
	dprintf("UsbHubDriver::Init()\n");

	fUsbDevice = fNode->QueryBusInterface<UsbDevice>();

	size_t actualLength;
	CHECK_RET_MSG(fUsbDevice->SendRequest(
		USB_REQTYPE_DEVICE_IN | USB_REQTYPE_CLASS,
		USB_REQUEST_GET_DESCRIPTOR,
		(USB_DESCRIPTOR_HUB << 8) | 0,
		0,
		sizeof(usb_hub_descriptor),
		(void *)&fHubDescriptor,
		&actualLength
	), "[!] can't get hub descriptor\n");

	if (actualLength < 8) {
		TRACE_ERROR("[!] bad hub descriptor\n");
		return B_BAD_VALUE;
	}

	TRACE("hub descriptor (%ld bytes):\n", actualLength);
	TRACE("\tlength:..............%d\n", fHubDescriptor.length);
	TRACE("\tdescriptor_type:.....0x%02x\n", fHubDescriptor.descriptor_type);
	TRACE("\tnum_ports:...........%d\n", fHubDescriptor.num_ports);
	TRACE("\tcharacteristics:.....0x%04x\n", fHubDescriptor.characteristics);
	TRACE("\tpower_on_to_power_g:.%d\n", fHubDescriptor.power_on_to_power_good);
	TRACE("\tdevice_removeable:...0x%02x\n", fHubDescriptor.device_removeable);
	TRACE("\tpower_control_mask:..0x%02x\n", fHubDescriptor.power_control_mask);

	if (fHubDescriptor.num_ports > USB_MAX_PORT_COUNT) {
		TRACE_ALWAYS("hub supports more ports than we do (%d vs. %d)\n",
			fHubDescriptor.num_ports, USB_MAX_PORT_COUNT);
		fHubDescriptor.num_ports = USB_MAX_PORT_COUNT;
	}

	CHECK_RET(fUsbDevice->InitHub(fHubDescriptor));

	const usb_configuration_info* configuration = fUsbDevice->GetConfiguration();
	usb_interface_info* interface = configuration->interface[0].active;
	fInterruptPipe = interface->endpoint[0].handle;
	dprintf("  configuration: %p\n", configuration);
	dprintf("  interface: %p\n", interface);
	dprintf("  fInterruptPipe: %p\n", fInterruptPipe);

	// Enable port power on all ports
	for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
		status_t status = fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_SET_FEATURE, PORT_POWER, i + 1, 0, NULL, NULL);

		if (status < B_OK)
			TRACE_ERROR("power up failed on port %" B_PRId32 "\n", i);
	}

	// Wait for power to stabilize
	snooze(fHubDescriptor.power_on_to_power_good * 2000);

	TRACE_ALWAYS("initialised ok\n");

	// initial port scan
	for (int32 i = 0; i < fHubDescriptor.num_ports; i++) {
		int32 index = i + 1;
		UpdatePort(index);
	}

	if (fInterruptPipe != NULL)
		fInterruptPipe->QueueInterrupt(fInterruptStatus, sizeof(fInterruptStatus), InterruptCallback, this);

	return B_OK;
}


status_t
UsbHubDriver::UpdatePortStatus(uint8 index)
{
	// get the current port status
	size_t actualLength = 0;
	status_t result = fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_IN,
		USB_REQUEST_GET_STATUS, 0, index + 1, sizeof(usb_port_status),
		(void *)&fPortStatus[index], &actualLength);

	if (result < B_OK || actualLength < sizeof(usb_port_status)) {
		TRACE_ERROR("error updating port status\n");
		return B_ERROR;
	}

	return B_OK;
}


status_t
UsbHubDriver::ResetPort(uint8 index)
{
	status_t result = fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
		USB_REQUEST_SET_FEATURE, PORT_RESET, index + 1, 0, NULL, NULL);

	if (result < B_OK)
		return result;

	for (int32 i = 0; i < 10; i++) {
		snooze(USB_DELAY_PORT_RESET);

		result = UpdatePortStatus(index);
		if (result < B_OK)
			return result;

		if ((fPortStatus[index].change & PORT_STATUS_RESET) != 0
			|| (fPortStatus[index].status & PORT_STATUS_RESET) == 0) {
			// reset is done
			break;
		}
	}

	if ((fPortStatus[index].change & PORT_STATUS_RESET) == 0
			&& (fPortStatus[index].status & PORT_STATUS_RESET) != 0) {
		TRACE_ERROR("port %d won't reset (%#x, %#x)\n", index,
			fPortStatus[index].change, fPortStatus[index].status);
		return B_ERROR;
	}

	// clear the reset change
	result = fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
		USB_REQUEST_CLEAR_FEATURE, C_PORT_RESET, index + 1, 0, NULL, NULL);
	if (result < B_OK)
		return result;

	// wait for reset recovery
	snooze(USB_DELAY_PORT_RESET_RECOVERY);
	TRACE("port %d was reset successfully\n", index);
	return B_OK;
}


status_t
UsbHubDriver::DisablePort(uint8 index)
{
	return fUsbDevice->SendRequest(USB_REQTYPE_CLASS
		| USB_REQTYPE_OTHER_OUT, USB_REQUEST_CLEAR_FEATURE, PORT_ENABLE,
		index + 1, 0, NULL, NULL);
}


status_t
UsbHubDriver::DebouncePort(uint8 index)
{
	uint32 timeout = 0;
	uint32 stableTime = 0;
	while (timeout < USB_DEBOUNCE_TIMEOUT) {
		snooze(USB_DEBOUNCE_CHECK_INTERVAL);
		timeout += USB_DEBOUNCE_CHECK_INTERVAL;

		status_t result = UpdatePortStatus(index);
		if (result != B_OK)
			return result;

		if ((fPortStatus[index].change & PORT_STATUS_CONNECTION) == 0) {
			stableTime += USB_DEBOUNCE_CHECK_INTERVAL;
			if (stableTime >= USB_DEBOUNCE_STABLE_TIME)
				return B_OK;
			continue;
		}

		// clear the connection change and reset stable time
		result = fUsbDevice->SendRequest(USB_REQTYPE_CLASS
			| USB_REQTYPE_OTHER_OUT, USB_REQUEST_CLEAR_FEATURE,
			C_PORT_CONNECTION, index + 1, 0, NULL, NULL);
		if (result != B_OK)
			return result;

		TRACE("got connection change during debounce, resetting stable time\n");
		stableTime = 0;
	}

	return B_TIMED_OUT;
}


void
UsbHubDriver::UpdatePort(uint8 index)
{
	if (index < 1)
		return;

	uint32 i = index - 1;

	status_t result = UpdatePortStatus(i);
	if (result < B_OK)
		return;

#ifdef TRACE_USB
	if (fPortStatus[i].change) {
		TRACE("port %" B_PRId32 ": status: 0x%04x; change: 0x%04x\n", i,
			fPortStatus[i].status, fPortStatus[i].change);
		TRACE("device at port %" B_PRId32 ": %p\n", i, fChildren[i]);
	}
#endif

	if ((fPortStatus[i].change & PORT_STATUS_CONNECTION)
			|| ((fPortStatus[i].status & PORT_STATUS_CONNECTION)
				&& fChildren[i] == NULL)) {
		// clear status change
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_CONNECTION, i + 1,
			0, NULL, NULL);

		if (fPortStatus[i].status & PORT_STATUS_CONNECTION) {
			// new device attached!
			TRACE_ALWAYS("port %" B_PRId32 ": new device connected\n", i);

			int32 retry = 2;
			while (retry--) {
				// wait for stable device power
				result = DebouncePort(i);
				if (result != B_OK) {
					TRACE_ERROR("debouncing port %" B_PRId32
						" failed: %s\n", i, strerror(result));
					break;
				}

				// reset the port, this will also enable it
				result = ResetPort(i);
				if (result < B_OK) {
					TRACE_ERROR("resetting port %" B_PRId32 " failed\n",
						i);
					break;
				}

				result = UpdatePortStatus(i);
				if (result < B_OK)
					break;

				if ((fPortStatus[i].status & PORT_STATUS_CONNECTION) == 0) {
					// device has vanished after reset, ignore
					TRACE("device disappeared on reset\n");
					break;
				}

				if (fChildren[i] != NULL) {
					TRACE_ERROR("new device on a port that is already in "
						"use\n");
					fUsbDevice->FreeDevice(fChildren[i]);
					fChildren[i] = NULL;
				}

				// Determine the device speed.
				usb_speed speed;

				// PORT_STATUS_LOW_SPEED and PORT_STATUS_SS_POWER are the
				// same, but PORT_STATUS_POWER will not be set for SS
				// devices, hence this somewhat convoluted logic.
				if ((fPortStatus[i].status & PORT_STATUS_POWER) != 0) {
					if ((fPortStatus[i].status & PORT_STATUS_HIGH_SPEED) != 0)
						speed = USB_SPEED_HIGHSPEED;
					else if ((fPortStatus[i].status & PORT_STATUS_LOW_SPEED) != 0)
						speed = USB_SPEED_LOWSPEED;
					else
						speed = USB_SPEED_FULLSPEED;
				} else {
					// This must be a SuperSpeed device, which will
					// simply inherit our speed.
					speed = fUsbDevice->Speed();
				}
				if (speed > fUsbDevice->Speed())
					speed = fUsbDevice->Speed();

				uint8 hubPort = i + 1;
				UsbDevice* newDevice = NULL;
				if (fUsbDevice->AllocateDevice(hubPort, speed, &newDevice) >= B_OK) {
					fChildren[i] = newDevice;
					break;
				} else {
					// the device failed to setup correctly, disable the
					// port so that the device doesn't get in the way of
					// future addressing.
					DisablePort(i);
				}
			}
		} else {
			// Device removed...
			TRACE_ALWAYS("port %" B_PRId32 ": device removed\n", i);
			if (fChildren[i] != NULL) {
				TRACE("removing device %p\n", fChildren[i]);
				fUsbDevice->FreeDevice(fChildren[i]);
				fChildren[i] = NULL;
			}
		}
	}

	// other port changes we do not really handle, report and clear them
	if (fPortStatus[i].change & PORT_STATUS_ENABLE) {
		TRACE_ALWAYS("port %" B_PRId32 " %sabled\n", i,
			(fPortStatus[i].status & PORT_STATUS_ENABLE) ? "en" : "dis");
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_ENABLE, i + 1,
			0, NULL, NULL);
	}

	if (fPortStatus[i].change & PORT_STATUS_SUSPEND) {
		TRACE_ALWAYS("port %" B_PRId32 " is %ssuspended\n", i,
			(fPortStatus[i].status & PORT_STATUS_SUSPEND) ? "" : "not ");
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_SUSPEND, i + 1,
			0, NULL, NULL);
	}

	if (fPortStatus[i].change & PORT_STATUS_OVER_CURRENT) {
		TRACE_ALWAYS("port %" B_PRId32 " is %sin an over current state\n",
			i, (fPortStatus[i].status & PORT_STATUS_OVER_CURRENT) ? "" : "not ");
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_OVER_CURRENT, i + 1,
			0, NULL, NULL);
	}

	if (fPortStatus[i].change & PORT_STATUS_RESET) {
		TRACE_ALWAYS("port %" B_PRId32 " was reset\n", i);
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_RESET, i + 1,
			0, NULL, NULL);
	}

	if (fPortStatus[i].change & PORT_CHANGE_LINK_STATE) {
		TRACE_ALWAYS("port %" B_PRId32 " link state changed\n", i);
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_LINK_STATE, i + 1,
			0, NULL, NULL);
	}

	if (fPortStatus[i].change & PORT_CHANGE_BH_PORT_RESET) {
		TRACE_ALWAYS("port %" B_PRId32 " was warm reset\n", i);
		fUsbDevice->SendRequest(USB_REQTYPE_CLASS | USB_REQTYPE_OTHER_OUT,
			USB_REQUEST_CLEAR_FEATURE, C_PORT_BH_PORT_RESET, i + 1,
			0, NULL, NULL);
	}
}


void
UsbHubDriver::InterruptCallback(void *cookie, status_t status, void *data,
	size_t actualLength)
{
	UsbHubDriver* hub = static_cast<UsbHubDriver*>(cookie);

	dprintf("UsbHubDriver::InterruptCallback(%p)\n", hub);

	uint8* bits = (uint8*)data;
	dprintf("  ports: {");
	bool isFirst = true;
	for (uint32 i = 0; i < 8*actualLength; i++) {
		if ((bits[i / 8] & (1 << (i % 8))) != 0) {
			if (isFirst) {isFirst = false;} else {dprintf(", ");}
			dprintf("%" B_PRIu32, i);
		}
	}
	dprintf("}\n");

	DPCQueue::DefaultQueue(B_LOW_PRIORITY)->Add(hub);
}


void
UsbHubDriver::DoDPC(DPCQueue* queue)
{
	uint8* bits = (uint8*)fInterruptStatus;
	for (uint32 i = 0; i < (uint32)fHubDescriptor.num_ports + 1; i++) {
		if ((bits[i / 8] & (1 << (i % 8))) != 0)
			UpdatePort(i);
	}

	fInterruptPipe->QueueInterrupt(fInterruptStatus, sizeof(fInterruptStatus), InterruptCallback, this);
}


driver_module_info gUsbHubDriverModule = {
	.info = {
		.name = USB_HUB_DRIVER_MODULE_NAME,
	},
	.probe = UsbHubDriver::Probe
};
