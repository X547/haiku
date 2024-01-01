/*
	Driver for USB Ethernet Control Model devices
	Copyright (C) 2008 Michael Lotz <mmlr@mlotz.ch>
	Distributed under the terms of the MIT license.
*/
#include "usb_ecm.h"

#include <stdio.h>
#include <string.h>
#include <new>

#include <condition_variable.h>
#include <ether_driver.h>
#include <net/if_media.h>

#include <AutoDeleter.h>


#define DRIVER_NAME	"usb_ecm"
#define USB_ECM_DRIVER_MODULE_NAME "drivers/network/usb_ecm/driver/v1"
#define DEVICE_BASE_NAME "net/usb_ecm/"


status_t
UsbEcmDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<UsbEcmDriver> driver(new(std::nothrow) UsbEcmDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->_Init());
	*outDriver = driver.Detach();
	return B_OK;
}


UsbEcmDriver::~UsbEcmDriver()
{
	if (fOpen && !fRemoved)
		fNotifyEndpoint->CancelQueuedTransfers();

	free(fNotifyBuffer);
}


void
UsbEcmDriver::DeviceRemoved()
{
	fRemoved = true;
	fHasConnection = false;
	fDownstreamSpeed = fUpstreamSpeed = 0;

	// the notify hook is different from the read and write hooks as it does
	// itself schedule traffic (while the other hooks only release a semaphore
	// to notify another thread which in turn safly checks for the removed
	// case) - so we must ensure that we are not inside the notify hook anymore
	// before returning, as we would otherwise violate the promise not to use
	// any of the pipes after returning from the removed hook
	while (atomic_add(&fInsideNotify, 0) != 0)
		snooze(100);

	if (fOpen) {
		fNotifyEndpoint->CancelQueuedTransfers();
		fReadEndpoint->CancelQueuedTransfers();
		fWriteEndpoint->CancelQueuedTransfers();
	}

	if (fLinkStateChangeSem >= B_OK)
		release_sem_etc(fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);
}


status_t
UsbEcmDriver::_Init()
{
	fDevice = fNode->QueryBusInterface<UsbDevice>();

	const usb_device_descriptor* deviceDescriptor = fDevice->GetDeviceDescriptor();

	if (deviceDescriptor == NULL) {
		TRACE_ALWAYS("failed to get device descriptor\n");
		return B_ERROR;
	}

	fVendorID = deviceDescriptor->vendor_id;
	fProductID = deviceDescriptor->product_id;

	fNotifyBufferLength = 64;
	fNotifyBuffer = (uint8 *)malloc(fNotifyBufferLength);
	if (fNotifyBuffer == NULL) {
		TRACE_ALWAYS("out of memory for notify buffer allocation\n");
		return B_ERROR;
	}

	if (_SetupDevice() != B_OK) {
		TRACE_ALWAYS("failed to setup device\n");
		return B_ERROR;
	}

	if (_ReadMACAddress() != B_OK) {
		TRACE_ALWAYS("failed to read mac address\n");
		return B_ERROR;
	}

	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), DEVICE_BASE_NAME "%" B_PRId32, id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


void
UsbEcmDriver::_NotifyCallback(void *cookie, int32 status, void *data, size_t actualLength)
{
	UsbEcmDriver *device = (UsbEcmDriver *)cookie;
	atomic_add(&device->fInsideNotify, 1);
	if (status == B_CANCELED || device->fRemoved) {
		atomic_add(&device->fInsideNotify, -1);
		return;
	}

	if (status == B_OK && actualLength >= sizeof(cdc_notification)) {
		bool linkStateChange = false;
		cdc_notification *notification
			= (cdc_notification *)device->fNotifyBuffer;

		switch (notification->notification_code) {
			case CDC_NOTIFY_NETWORK_CONNECTION:
				TRACE("connection state change to %d\n", notification->value);
				device->fHasConnection = notification->value > 0;
				linkStateChange = true;
				break;

			case CDC_NOTIFY_CONNECTION_SPEED_CHANGE:
			{
				if (notification->data_length < sizeof(cdc_connection_speed)
					|| actualLength < sizeof(cdc_notification)
					+ sizeof(cdc_connection_speed)) {
					TRACE_ALWAYS("not enough data in connection speed change\n");
					break;
				}

				cdc_connection_speed *speed;
				speed = (cdc_connection_speed *)&notification->data[0];
				device->fUpstreamSpeed = speed->upstream_speed;
				device->fDownstreamSpeed = speed->downstream_speed;
				device->fHasConnection = true;
				TRACE("connection speed change to %" B_PRId32 "/%" B_PRId32 "\n",
					speed->downstream_speed, speed->upstream_speed);
				linkStateChange = true;
				break;
			}

			default:
				TRACE_ALWAYS("unsupported notification 0x%02x\n",
					notification->notification_code);
				break;
		}

		if (linkStateChange && device->fLinkStateChangeSem >= B_OK)
			release_sem_etc(device->fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);
	}

	if (status != B_OK) {
		TRACE_ALWAYS("device status error 0x%08" B_PRIx32 "\n", status);
		if (device->fNotifyEndpoint->GetObject()->ClearFeature(
			USB_FEATURE_ENDPOINT_HALT) != B_OK)
			TRACE_ALWAYS("failed to clear halt state in notify hook\n");
	}

	// schedule next notification buffer
	device->fNotifyEndpoint->QueueInterrupt(device->fNotifyBuffer,
		device->fNotifyBufferLength, _NotifyCallback, device);
	atomic_add(&device->fInsideNotify, -1);
}


status_t
UsbEcmDriver::_SetupDevice()
{
	const usb_device_descriptor* deviceDescriptor = fDevice->GetDeviceDescriptor();

	if (deviceDescriptor == NULL) {
		TRACE_ALWAYS("failed to get device descriptor\n");
		return B_ERROR;
	}

	uint8 controlIndex = 0;
	uint8 dataIndex = 0;
	bool foundUnionDescriptor = false;
	bool foundEthernetDescriptor = false;
	bool found = false;
	const usb_configuration_info *config = NULL;
	for (int i = 0; i < deviceDescriptor->num_configurations && !found; i++) {
		config = fDevice->GetNthConfiguration(i);
		if (config == NULL)
			continue;

		for (size_t j = 0; j < config->interface_count && !found; j++) {
			const usb_interface_info *interface = config->interface[j].active;
			usb_interface_descriptor *descriptor = interface->descr;
			if (descriptor->interface_class != USB_INTERFACE_CLASS_CDC
				|| descriptor->interface_subclass != USB_INTERFACE_SUBCLASS_ECM
				|| interface->generic_count == 0) {
				continue;
			}

			// try to find and interpret the union and ethernet functional
			// descriptors
			foundUnionDescriptor = foundEthernetDescriptor = false;
			for (size_t k = 0; k < interface->generic_count; k++) {
				usb_generic_descriptor *generic = &interface->generic[k]->generic;
				if (generic->length >= 5
					&& generic->data[0] == FUNCTIONAL_SUBTYPE_UNION) {
					controlIndex = generic->data[1];
					dataIndex = generic->data[2];
					foundUnionDescriptor = true;
				} else if (generic->length >= sizeof(ethernet_functional_descriptor)
					&& generic->data[0] == FUNCTIONAL_SUBTYPE_ETHERNET) {
					ethernet_functional_descriptor *ethernet
						= (ethernet_functional_descriptor *)generic->data;
					fMACAddressIndex = ethernet->mac_address_index;
					fMaxSegmentSize = ethernet->max_segment_size;
					foundEthernetDescriptor = true;
				}

				if (foundUnionDescriptor && foundEthernetDescriptor) {
					found = true;
					break;
				}
			}
		}
	}

	if (!foundUnionDescriptor) {
		TRACE_ALWAYS("did not find a union descriptor\n");
		return B_ERROR;
	}

	if (!foundEthernetDescriptor) {
		TRACE_ALWAYS("did not find an ethernet descriptor\n");
		return B_ERROR;
	}

	// set the current configuration
	fDevice->SetConfiguration(config);
	if (controlIndex >= config->interface_count) {
		TRACE_ALWAYS("control interface index invalid\n");
		return B_ERROR;
	}

	// check that the indicated control interface fits our needs
	usb_interface_info *interface = config->interface[controlIndex].active;
	usb_interface_descriptor *descriptor = interface->descr;
	if ((descriptor->interface_class != USB_INTERFACE_CLASS_CDC
		|| descriptor->interface_subclass != USB_INTERFACE_SUBCLASS_ECM)
		|| interface->endpoint_count == 0) {
		TRACE_ALWAYS("control interface invalid\n");
		return B_ERROR;
	}

	fControlInterfaceIndex = controlIndex;

	if (dataIndex >= config->interface_count) {
		TRACE_ALWAYS("data interface index invalid\n");
		return B_ERROR;
	}

	// check that the indicated data interface fits our needs
	if (config->interface[dataIndex].alt_count < 2) {
		TRACE_ALWAYS("data interface does not provide two alternate interfaces\n");
		return B_ERROR;
	}

	// alternate 0 is the disabled, endpoint-less default interface
	interface = &config->interface[dataIndex].alt[1];
	descriptor = interface->descr;
	if (descriptor->interface_class != USB_INTERFACE_CLASS_CDC_DATA
		|| interface->endpoint_count < 2) {
		TRACE_ALWAYS("data interface invalid\n");
		return B_ERROR;
	}

	fDataInterfaceIndex = dataIndex;
	return B_OK;
}


status_t
UsbEcmDriver::_ReadMACAddress()
{
	if (fMACAddressIndex == 0)
		return B_BAD_VALUE;

	size_t actualLength = 0;
	size_t macStringLength = 26;
	uint8 macString[macStringLength];
	status_t result = fDevice->GetDescriptor(USB_DESCRIPTOR_STRING,
		fMACAddressIndex, 0, macString, macStringLength, &actualLength);
	if (result != B_OK)
		return result;

	if (actualLength != macStringLength) {
		TRACE_ALWAYS("did not retrieve full mac address\n");
		return B_ERROR;
	}

	char macPart[3];
	macPart[2] = 0;
	for (int32 i = 0; i < 6; i++) {
		macPart[0] = macString[2 + i * 4 + 0];
		macPart[1] = macString[2 + i * 4 + 2];
		fMACAddress[i] = strtol(macPart, NULL, 16);
	}

	TRACE_ALWAYS("read mac address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		fMACAddress[0], fMACAddress[1], fMACAddress[2], fMACAddress[3], fMACAddress[4], fMACAddress[5]);
	return B_OK;
}


DevFsNode::Capabilities
UsbEcmDriver::DevFsNode::GetCapabilities() const
{
	return {.read = true, .write = true, .control = true};
}


status_t
UsbEcmDriver::DevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	if (Base().fOpen)
		return B_BUSY;
	if (Base().fRemoved)
		return B_ERROR;

	// reset the device by switching the data interface to the disabled first
	// interface and then enable it by setting the second actual data interface
	const usb_configuration_info *config = Base().fDevice->GetConfiguration();

	Base().fDevice->SetAltInterface(&config->interface[Base().fDataInterfaceIndex].alt[0]);

	// update to the changed config
	config = Base().fDevice->GetConfiguration();
	Base().fDevice->SetAltInterface(&config->interface[Base().fDataInterfaceIndex].alt[1]);
	Base().fDevice->SetAltInterface(&config->interface[Base().fControlInterfaceIndex].alt[0]);

	usb_interface_info* interface = config->interface[Base().fControlInterfaceIndex].active;
	Base().fNotifyEndpoint = interface->endpoint[0].handle;
	Base().fNotifyBufferLength = interface->endpoint[0].descr->max_packet_size;

	// update again
	config = Base().fDevice->GetConfiguration();
	interface = config->interface[Base().fDataInterfaceIndex].active;
	if (interface->endpoint_count < 2) {
		TRACE_ALWAYS("setting the data alternate interface failed\n");
		return B_ERROR;
	}

	if (!(interface->endpoint[0].descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN))
		Base().fWriteEndpoint = interface->endpoint[0].handle;
	else
		Base().fReadEndpoint = interface->endpoint[0].handle;

	if (interface->endpoint[1].descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
		Base().fReadEndpoint = interface->endpoint[1].handle;
	else
		Base().fWriteEndpoint = interface->endpoint[1].handle;

	if (Base().fReadEndpoint == 0 || Base().fWriteEndpoint == 0) {
		TRACE_ALWAYS("no read and write endpoints found\n");
		return B_ERROR;
	}

	if (Base().fNotifyEndpoint->QueueInterrupt(Base().fNotifyBuffer,
		Base().fNotifyBufferLength, _NotifyCallback, &Base()) != B_OK) {
		// we cannot use notifications - hardcode to active connection
		Base().fHasConnection = true;
		Base().fDownstreamSpeed = 1000 * 1000 * 10; // 10Mbps
		Base().fUpstreamSpeed = 1000 * 1000 * 10; // 10Mbps
	}

	// the device should now be ready
	Base().fOpen = true;
	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
UsbEcmDriver::DevFsNode::Close()
{
	if (Base().fRemoved) {
		Base().fOpen = false;
		return B_OK;
	}

	Base().fNotifyEndpoint->CancelQueuedTransfers();
	Base().fReadEndpoint->CancelQueuedTransfers();
	Base().fWriteEndpoint->CancelQueuedTransfers();

	Base().fNotifyEndpoint = NULL;
	Base().fReadEndpoint = NULL;
	Base().fWriteEndpoint = NULL;

	// put the device into non-connected mode again by switching the data
	// interface to the disabled alternate
	const usb_configuration_info *config = Base().fDevice->GetConfiguration();

	Base().fDevice->SetAltInterface(&config->interface[Base().fDataInterfaceIndex].alt[0]);

	Base().fOpen = false;
	return B_OK;
}


status_t
UsbEcmDriver::DevFsNode::Read(off_t pos, void* buffer, size_t* numBytes)
{
	TRACE("Read(%" B_PRIuSIZE ")\n", *numBytes);
	if (Base().fRemoved) {
		*numBytes = 0;
		return B_DEVICE_NOT_FOUND;
	}

	ConditionVariable cv;
	cv.Init(this, "UsbEcmDriver::DevFsNode::Read");
	ConditionVariableEntry cvEntry;
	cv.Add(&cvEntry);

	status_t ioStatus;
	auto Callback = [numBytes, &cv, &ioStatus](int32 status, void *data, size_t actualLength) {
		*numBytes = actualLength;
		ioStatus = status;
		cv.NotifyOne(B_OK);
	};

	auto CallbackWrapper = [](void *cookie, int32 status, void *data, size_t actualLength) {
		(*(decltype(Callback)*)cookie)(status, data, actualLength);
	};

	status_t result = Base().fReadEndpoint->QueueBulk(buffer, *numBytes, CallbackWrapper, &Callback);
	if (result != B_OK) {
		*numBytes = 0;
		return result;
	}

	result = cvEntry.Wait(B_CAN_INTERRUPT, 0);
	if (result < B_OK) {
		*numBytes = 0;
		return result;
	}

	if (ioStatus != B_OK && ioStatus != B_CANCELED && !Base().fRemoved) {
		TRACE_ALWAYS("device status error 0x%08" B_PRIx32 "\n", ioStatus);
		result = Base().fReadEndpoint->GetObject()->ClearFeature(USB_FEATURE_ENDPOINT_HALT);
		if (result != B_OK) {
			TRACE_ALWAYS("failed to clear halt state on read\n");
			*numBytes = 0;
			return result;
		}
	}

	TRACE_ALWAYS("read done: %" B_PRIuSIZE "\n", *numBytes);
	return B_OK;
}


status_t
UsbEcmDriver::DevFsNode::Write(off_t pos, const void* buffer, size_t* numBytes)
{
	TRACE("Write(%" B_PRIuSIZE ")\n", *numBytes);
	if (Base().fRemoved) {
		*numBytes = 0;
		return B_DEVICE_NOT_FOUND;
	}

	ConditionVariable cv;
	cv.Init(this, "UsbEcmDriver::DevFsNode::Write");
	ConditionVariableEntry cvEntry;
	cv.Add(&cvEntry);

	status_t ioStatus;
	auto Callback = [numBytes, &cv, &ioStatus](int32 status, void *data, size_t actualLength) {
		*numBytes = actualLength;
		ioStatus = status;
		cv.NotifyOne(B_OK);
	};

	auto CallbackWrapper = [](void *cookie, int32 status, void *data, size_t actualLength) {
		(*(decltype(Callback)*)cookie)(status, data, actualLength);
	};

	status_t result = Base().fWriteEndpoint->QueueBulk((uint8 *)buffer, *numBytes, CallbackWrapper, &Callback);
	if (result != B_OK) {
		*numBytes = 0;
		return result;
	}

	result = cvEntry.Wait(B_CAN_INTERRUPT, 0);
	if (result < B_OK) {
		*numBytes = 0;
		return result;
	}

	if (ioStatus != B_OK && ioStatus != B_CANCELED && !Base().fRemoved) {
		TRACE_ALWAYS("device status error 0x%08" B_PRIx32 "\n", ioStatus);
		result = Base().fWriteEndpoint->GetObject()->ClearFeature(
			USB_FEATURE_ENDPOINT_HALT);
		if (result != B_OK) {
			TRACE_ALWAYS("failed to clear halt state on write\n");
			*numBytes = 0;
			return result;
		}
	}

	TRACE_ALWAYS("write done: %" B_PRIuSIZE "\n", *numBytes);
	return B_OK;
}


status_t
UsbEcmDriver::DevFsNode::Control(uint32 op, void *buffer, size_t length, bool isKernel)
{
	switch (op) {
		case ETHER_INIT:
			return B_OK;

		case ETHER_GETADDR:
			memcpy(buffer, &Base().fMACAddress, sizeof(Base().fMACAddress));
			return B_OK;

		case ETHER_GETFRAMESIZE:
			*(uint32 *)buffer = Base().fMaxSegmentSize;
			return B_OK;

		case ETHER_SET_LINK_STATE_SEM:
			Base().fLinkStateChangeSem = *(sem_id *)buffer;
			return B_OK;

		case ETHER_GET_LINK_STATE:
		{
			ether_link_state *state = (ether_link_state *)buffer;
			state->media = IFM_ETHER | IFM_FULL_DUPLEX
				| (Base().fHasConnection ? IFM_ACTIVE : 0);
			state->quality = 1000;
			state->speed = Base().fDownstreamSpeed;
			return B_OK;
		}

		default:
			TRACE_ALWAYS("unsupported ioctl %" B_PRIu32 "\n", op);
	}

	return B_DEV_INVALID_IOCTL;
}


static driver_module_info sUsbEcmDriver = {
	.info = {
		.name = USB_ECM_DRIVER_MODULE_NAME,
	},
	.probe = UsbEcmDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sUsbEcmDriver,
	NULL
};
