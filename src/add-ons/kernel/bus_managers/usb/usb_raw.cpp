/*
 * Copyright 2006-2018, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Adrien Destugues <pulkomandy@pulkomandy.tk>
 */

#include "usb_raw_private.h"
#include "usb_raw.h"

#include <algorithm>

#include <AutoDeleter.h>

#include <condition_variable.h>
#include <kernel.h>


//#define TRACE_USB_RAW
#ifdef TRACE_USB_RAW
#define TRACE(x)	dprintf x
#else
#define TRACE(x)	/* nothing */
#endif

#define DRIVER_NAME		"usb_raw"


struct CommandResult {
	ConditionVariable cond;
	ConditionVariableEntry entry;
	status_t status {};
	size_t actualLength {};

	CommandResult()
	{
		cond.Init(this, "CommandResult");
		cond.Add(&entry);
	}

	status_t Wait()
	{
		return entry.Wait(B_KILL_CAN_INTERRUPT);
	}

	static void Callback(void* cookie, status_t status, void* data, size_t actualLength);
};


void
CommandResult::Callback(void* cookie, status_t status, void* data, size_t actualLength)
{
	CommandResult* result = (CommandResult*)cookie;

	switch (status) {
		case B_OK:
			result->status = B_USB_RAW_STATUS_SUCCESS;
			break;
		case B_TIMED_OUT:
			result->status = B_USB_RAW_STATUS_TIMEOUT;
			break;
		case B_CANCELED:
			result->status = B_USB_RAW_STATUS_ABORTED;
			break;
		case B_DEV_CRC_ERROR:
			result->status = B_USB_RAW_STATUS_CRC_ERROR;
			break;
		case B_DEV_STALLED:
			result->status = B_USB_RAW_STATUS_STALLED;
			break;
		default:
			result->status = B_USB_RAW_STATUS_FAILED;
			break;
	}

	result->actualLength = actualLength;
	result->cond.NotifyAll();
}


status_t
UsbDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle** outHandle)
{
	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
UsbDevFsNode::Control(uint32 op, void* buffer, size_t length, bool isKernel)
{
	usb_raw_command command;
	if (length < sizeof(command.version.status))
		return B_BUFFER_OVERFLOW;
	if (!IS_USER_ADDRESS(buffer)
		|| user_memcpy(&command, buffer, min_c(length, sizeof(command)))
			!= B_OK) {
		return B_BAD_ADDRESS;
	}

	command.version.status = B_USB_RAW_STATUS_ABORTED;
	status_t status = B_DEV_INVALID_IOCTL;

	switch (op) {
		case B_USB_RAW_COMMAND_GET_VERSION:
		{
			command.version.status = B_USB_RAW_PROTOCOL_VERSION;
			status = B_OK;
			break;
		}

		case B_USB_RAW_COMMAND_GET_DEVICE_DESCRIPTOR:
		{
			if (length < sizeof(command.device))
				return B_BUFFER_OVERFLOW;

			status = B_OK;
			const usb_device_descriptor* deviceDescriptor =
				fDevice->GetDeviceDescriptor();
			if (deviceDescriptor == NULL)
				return B_OK;

			if (!IS_USER_ADDRESS(command.device.descriptor)
				|| user_memcpy(command.device.descriptor, deviceDescriptor,
					sizeof(usb_device_descriptor)) != B_OK) {
				return B_BAD_ADDRESS;
			}

			command.device.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR_ETC:
		{
			if (op == B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR_ETC
					&& length < sizeof(command.config_etc))
				return B_BUFFER_OVERFLOW;

			if (length < sizeof(command.config))
				return B_BUFFER_OVERFLOW;

			status = B_OK;
			const usb_configuration_info* configurationInfo =
				GetConfiguration(command.config.config_index,
					&command.config.status);
			if (configurationInfo == NULL)
				break;

			size_t sizeToCopy = sizeof(usb_configuration_descriptor);
			if (op == B_USB_RAW_COMMAND_GET_CONFIGURATION_DESCRIPTOR_ETC) {
				sizeToCopy = std::min(command.config_etc.length,
					(size_t)configurationInfo->descr->total_length);
			}

			if (!IS_USER_ADDRESS(command.config.descriptor)
				|| user_memcpy(command.config.descriptor,
					configurationInfo->descr, sizeToCopy) != B_OK) {
				return B_BAD_ADDRESS;
			}

			command.config.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_ALT_INTERFACE_COUNT:
		case B_USB_RAW_COMMAND_GET_ACTIVE_ALT_INTERFACE_INDEX:
		{
			if (length < sizeof(command.alternate))
				return B_BUFFER_OVERFLOW;

			status = B_OK;
			const usb_configuration_info* configurationInfo =
				GetConfiguration(
					command.alternate.config_index,
					&command.alternate.status);
			if (configurationInfo == NULL)
				break;

			if (command.alternate.interface_index
				>= configurationInfo->interface_count) {
				command.alternate.status = B_USB_RAW_STATUS_INVALID_INTERFACE;
				break;
			}

			const usb_interface_list* interfaceList
				= &configurationInfo->interface[
					command.alternate.interface_index];
			if (op == B_USB_RAW_COMMAND_GET_ALT_INTERFACE_COUNT) {
				command.alternate.alternate_info = interfaceList->alt_count;
			} else {
				for (size_t i = 0; i < interfaceList->alt_count; i++) {
					if (&interfaceList->alt[i] == interfaceList->active) {
						command.alternate.alternate_info = i;
						break;
					}
				}
			}

			command.alternate.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR_ETC:
		{
			const usb_interface_info* interfaceInfo = NULL;
			status = B_OK;
			if (op == B_USB_RAW_COMMAND_GET_INTERFACE_DESCRIPTOR) {
				if (length < sizeof(command.interface))
					return B_BUFFER_OVERFLOW;

				interfaceInfo = GetInterface(
					command.interface.config_index,
					command.interface.interface_index,
					B_USB_RAW_ACTIVE_ALTERNATE,
					&command.interface.status);
			} else {
				if (length < sizeof(command.interface_etc))
					return B_BUFFER_OVERFLOW;

				interfaceInfo = GetInterface(
					command.interface_etc.config_index,
					command.interface_etc.interface_index,
					command.interface_etc.alternate_index,
					&command.interface_etc.status);
			}

			if (interfaceInfo == NULL)
				break;

			if (!IS_USER_ADDRESS(command.interface.descriptor)
				|| user_memcpy(command.interface.descriptor, interfaceInfo->descr,
					sizeof(usb_interface_descriptor)) != B_OK) {
				return B_BAD_ADDRESS;
			}

			command.interface.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR_ETC:
		{
			uint32 endpointIndex = 0;
			const usb_interface_info* interfaceInfo = NULL;
			status = B_OK;
			if (op == B_USB_RAW_COMMAND_GET_ENDPOINT_DESCRIPTOR) {
				if (length < sizeof(command.endpoint))
					return B_BUFFER_OVERFLOW;

				interfaceInfo = GetInterface(
					command.endpoint.config_index,
					command.endpoint.interface_index,
					B_USB_RAW_ACTIVE_ALTERNATE,
					&command.endpoint.status);
				endpointIndex = command.endpoint.endpoint_index;
			} else {
				if (length < sizeof(command.endpoint_etc))
					return B_BUFFER_OVERFLOW;

				interfaceInfo = GetInterface(
					command.endpoint_etc.config_index,
					command.endpoint_etc.interface_index,
					command.endpoint_etc.alternate_index,
					&command.endpoint_etc.status);
				endpointIndex = command.endpoint_etc.endpoint_index;
			}

			if (interfaceInfo == NULL)
				break;

			if (endpointIndex >= interfaceInfo->endpoint_count) {
				command.endpoint.status = B_USB_RAW_STATUS_INVALID_ENDPOINT;
				break;
			}

			if (!IS_USER_ADDRESS(command.endpoint.descriptor)
				|| user_memcpy(command.endpoint.descriptor,
					interfaceInfo->endpoint[endpointIndex].descr,
					sizeof(usb_endpoint_descriptor)) != B_OK) {
				return B_BAD_ADDRESS;
			}

			command.endpoint.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR:
		case B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR_ETC:
		{
			uint32 genericIndex = 0;
			size_t genericLength = 0;
			const usb_interface_info* interfaceInfo = NULL;
			status = B_OK;
			if (op == B_USB_RAW_COMMAND_GET_GENERIC_DESCRIPTOR) {
				if (length < sizeof(command.generic))
					return B_BUFFER_OVERFLOW;

				interfaceInfo = GetInterface(
					command.generic.config_index,
					command.generic.interface_index,
					B_USB_RAW_ACTIVE_ALTERNATE,
					&command.generic.status);
				genericIndex = command.generic.generic_index;
				genericLength = command.generic.length;
			} else {
				if (length < sizeof(command.generic_etc))
					return B_BUFFER_OVERFLOW;

				interfaceInfo = GetInterface(
					command.generic_etc.config_index,
					command.generic_etc.interface_index,
					command.generic_etc.alternate_index,
					&command.generic_etc.status);
				genericIndex = command.generic_etc.generic_index;
				genericLength = command.generic_etc.length;
			}

			if (interfaceInfo == NULL)
				break;

			if (genericIndex >= interfaceInfo->generic_count) {
				command.endpoint.status = B_USB_RAW_STATUS_INVALID_ENDPOINT;
				break;
			}

			usb_descriptor* descriptor = interfaceInfo->generic[genericIndex];
			if (descriptor == NULL)
				break;

			if (!IS_USER_ADDRESS(command.generic.descriptor)
				|| user_memcpy(command.generic.descriptor, descriptor,
					min_c(genericLength, descriptor->generic.length)) != B_OK) {
				return B_BAD_ADDRESS;
			}

			if (descriptor->generic.length > genericLength)
				command.generic.status = B_USB_RAW_STATUS_NO_MEMORY;
			else
				command.generic.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_GET_STRING_DESCRIPTOR:
		{
			if (length < sizeof(command.string))
				return B_BUFFER_OVERFLOW;

			size_t actualLength = 0;
			uint8 temp[4];
			status = B_OK;

			// We need to fetch the default language code, first.
			if (fDevice->GetDescriptor(
				USB_DESCRIPTOR_STRING, 0, 0,
				temp, 4, &actualLength) < B_OK
				|| actualLength != 4
				|| temp[1] != USB_DESCRIPTOR_STRING) {
				command.string.status = B_USB_RAW_STATUS_ABORTED;
				command.string.length = 0;
				break;
			}
			const uint16 langid = (temp[2] | (temp[3] << 8));

			// Now fetch the string length.
			if (fDevice->GetDescriptor(
				USB_DESCRIPTOR_STRING, command.string.string_index, langid,
				temp, 2, &actualLength) < B_OK
				|| actualLength != 2
				|| temp[1] != USB_DESCRIPTOR_STRING) {
				command.string.status = B_USB_RAW_STATUS_ABORTED;
				command.string.length = 0;
				break;
			}

			uint8 stringLength = std::min<uint8>(temp[0], command.string.length);
			char* string = (char*)malloc(stringLength);
			if (string == NULL) {
				command.string.status = B_USB_RAW_STATUS_ABORTED;
				command.string.length = 0;
				status = B_NO_MEMORY;
				break;
			}

			if (fDevice->GetDescriptor(
				USB_DESCRIPTOR_STRING, command.string.string_index, langid,
				string, stringLength, &actualLength) < B_OK
				|| actualLength != stringLength) {
				command.string.status = B_USB_RAW_STATUS_ABORTED;
				command.string.length = 0;
				free(string);
				break;
			}

			if (!IS_USER_ADDRESS(command.string.descriptor)
				|| user_memcpy(command.string.descriptor, string,
					stringLength) != B_OK) {
				free(string);
				return B_BAD_ADDRESS;
			}

			command.string.status = B_USB_RAW_STATUS_SUCCESS;
			command.string.length = stringLength;
			free(string);
			break;
		}

		case B_USB_RAW_COMMAND_GET_DESCRIPTOR:
		{
			if (length < sizeof(command.descriptor))
				return B_BUFFER_OVERFLOW;

			size_t actualLength = 0;
			uint8 firstTwoBytes[2];
			status = B_OK;

			if (fDevice->GetDescriptor(
				command.descriptor.type, command.descriptor.index,
				command.descriptor.language_id, firstTwoBytes, 2,
				&actualLength) < B_OK
				|| actualLength != 2
				|| firstTwoBytes[1] != command.descriptor.type) {
				command.descriptor.status = B_USB_RAW_STATUS_ABORTED;
				command.descriptor.length = 0;
				break;
			}

			uint8 descriptorLength = std::min<uint8>(firstTwoBytes[0],
				command.descriptor.length);
			uint8* descriptorBuffer = (uint8*)malloc(descriptorLength);
			if (descriptorBuffer == NULL) {
				command.descriptor.status = B_USB_RAW_STATUS_ABORTED;
				command.descriptor.length = 0;
				status = B_NO_MEMORY;
				break;
			}

			if (fDevice->GetDescriptor(
				command.descriptor.type, command.descriptor.index,
				command.descriptor.language_id, descriptorBuffer,
				descriptorLength, &actualLength) < B_OK
				|| actualLength != descriptorLength) {
				command.descriptor.status = B_USB_RAW_STATUS_ABORTED;
				command.descriptor.length = 0;
				free(descriptorBuffer);
				break;
			}

			if (!IS_USER_ADDRESS(command.descriptor.data)
				|| user_memcpy(command.descriptor.data, descriptorBuffer,
					descriptorLength) != B_OK) {
				free(descriptorBuffer);
				return B_BAD_ADDRESS;
			}

			command.descriptor.status = B_USB_RAW_STATUS_SUCCESS;
			command.descriptor.length = descriptorLength;
			free(descriptorBuffer);
			break;
		}

		case B_USB_RAW_COMMAND_SET_CONFIGURATION:
		{
			if (length < sizeof(command.config))
				return B_BUFFER_OVERFLOW;

			status = B_OK;
			const usb_configuration_info* configurationInfo =
				GetConfiguration(command.config.config_index,
					&command.config.status);
			if (configurationInfo == NULL)
				break;

			if (fDevice->SetConfiguration(configurationInfo) < B_OK) {
				command.config.status = B_USB_RAW_STATUS_FAILED;
				break;
			}

			command.config.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_SET_ALT_INTERFACE:
		{
			if (length < sizeof(command.alternate))
				return B_BUFFER_OVERFLOW;

			status = B_OK;
			const usb_configuration_info* configurationInfo =
				GetConfiguration(
					command.alternate.config_index,
					&command.alternate.status);
			if (configurationInfo == NULL)
				break;

			if (command.alternate.interface_index
				>= configurationInfo->interface_count) {
				command.alternate.status = B_USB_RAW_STATUS_INVALID_INTERFACE;
				break;
			}

			const usb_interface_list* interfaceList =
				&configurationInfo->interface[command.alternate.interface_index];
			if (command.alternate.alternate_info >= interfaceList->alt_count) {
				command.alternate.status = B_USB_RAW_STATUS_INVALID_INTERFACE;
				break;
			}

			if (fDevice->SetAltInterface(
				&interfaceList->alt[command.alternate.alternate_info]) < B_OK) {
				command.alternate.status = B_USB_RAW_STATUS_FAILED;
				break;
			}

			command.alternate.status = B_USB_RAW_STATUS_SUCCESS;
			break;
		}

		case B_USB_RAW_COMMAND_CONTROL_TRANSFER:
		{
			if (length < sizeof(command.control))
				return B_BUFFER_OVERFLOW;

			void* controlData = malloc(command.control.length);
			if (controlData == NULL)
				return B_NO_MEMORY;
			MemoryDeleter dataDeleter(controlData);

			bool inTransfer = (command.control.request_type
				& USB_ENDPOINT_ADDR_DIR_IN) != 0;
			if (!IS_USER_ADDRESS(command.control.data)
				|| (!inTransfer && user_memcpy(controlData,
					command.control.data, command.control.length) != B_OK)) {
				return B_BAD_ADDRESS;
			}

			CommandResult result;
			MutexLocker deviceLocker(fLock);
			if (fDevice->QueueRequest(
				command.control.request_type, command.control.request,
				command.control.value, command.control.index,
				command.control.length, controlData,
				CommandResult::Callback, &result) < B_OK) {
				command.control.status = B_USB_RAW_STATUS_FAILED;
				command.control.length = 0;
				status = B_OK;
				break;
			}

			status = result.Wait();
			if (status != B_OK)
				fDevice->CancelQueuedRequests();

			command.control.status = result.status;
			command.control.length = result.actualLength;
			deviceLocker.Unlock();

			if (command.control.status == B_OK)
				status = B_OK;
			if (inTransfer && user_memcpy(command.control.data, controlData,
				command.control.length) != B_OK) {
				status = B_BAD_ADDRESS;
			}
			break;
		}

		case B_USB_RAW_COMMAND_INTERRUPT_TRANSFER:
		case B_USB_RAW_COMMAND_BULK_TRANSFER:
		case B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER:
		{
			if (length < sizeof(command.transfer))
				return B_BUFFER_OVERFLOW;

			status = B_OK;
			const usb_configuration_info* configurationInfo =
				fDevice->GetConfiguration();
			if (configurationInfo == NULL) {
				command.transfer.status = B_USB_RAW_STATUS_INVALID_CONFIGURATION;
				break;
			}

			if (command.transfer.interface >= configurationInfo->interface_count) {
				command.transfer.status = B_USB_RAW_STATUS_INVALID_INTERFACE;
				break;
			}

			const usb_interface_info* interfaceInfo =
				configurationInfo->interface[command.transfer.interface].active;
			if (interfaceInfo == NULL) {
				command.transfer.status = B_USB_RAW_STATUS_ABORTED;
				break;
			}

			if (command.transfer.endpoint >= interfaceInfo->endpoint_count) {
				command.transfer.status = B_USB_RAW_STATUS_INVALID_ENDPOINT;
				break;
			}

			const usb_endpoint_info* endpointInfo =
				&interfaceInfo->endpoint[command.transfer.endpoint];
			if (!endpointInfo->handle) {
				command.transfer.status = B_USB_RAW_STATUS_INVALID_ENDPOINT;
				break;
			}

			size_t descriptorsSize = 0;
			usb_iso_packet_descriptor* packetDescriptors = NULL;
			void* transferData = NULL;
			MemoryDeleter descriptorsDeleter, dataDeleter;

			bool inTransfer = (endpointInfo->descr->endpoint_address
				& USB_ENDPOINT_ADDR_DIR_IN) != 0;
			if (op == B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER) {
				if (length < sizeof(command.isochronous))
					return B_BUFFER_OVERFLOW;

				descriptorsSize = sizeof(usb_iso_packet_descriptor)
					* command.isochronous.packet_count;
				packetDescriptors
					= (usb_iso_packet_descriptor*)malloc(descriptorsSize);
				if (packetDescriptors == NULL) {
					command.transfer.status = B_USB_RAW_STATUS_NO_MEMORY;
					command.transfer.length = 0;
					break;
				}
				descriptorsDeleter.SetTo(packetDescriptors);

				if (!IS_USER_ADDRESS(command.isochronous.data)
					|| !IS_USER_ADDRESS(command.isochronous.packet_descriptors)
					|| user_memcpy(packetDescriptors,
						command.isochronous.packet_descriptors,
						descriptorsSize) != B_OK) {
					return B_BAD_ADDRESS;
				}
			} else {
				transferData = malloc(command.transfer.length);
				if (transferData == NULL) {
					command.transfer.status = B_USB_RAW_STATUS_NO_MEMORY;
					command.transfer.length = 0;
					break;
				}
				dataDeleter.SetTo(transferData);

				if (!IS_USER_ADDRESS(command.transfer.data) || (!inTransfer
						&& user_memcpy(transferData, command.transfer.data,
							command.transfer.length) != B_OK)) {
					return B_BAD_ADDRESS;
				}
			}

			status_t status;
			CommandResult result;
			MutexLocker deviceLocker(fLock);
			if (op == B_USB_RAW_COMMAND_INTERRUPT_TRANSFER) {
				status = endpointInfo->handle->QueueInterrupt(
					transferData, command.transfer.length,
					CommandResult::Callback, &result);
			} else if (op == B_USB_RAW_COMMAND_BULK_TRANSFER) {
				status = endpointInfo->handle->QueueBulk(
					transferData, command.transfer.length,
					CommandResult::Callback, &result);
			} else {
				status = endpointInfo->handle->QueueIsochronous(
					command.isochronous.data, command.isochronous.length,
					packetDescriptors, command.isochronous.packet_count, NULL,
					0, CommandResult::Callback, &result);
			}

			if (status < B_OK) {
				command.transfer.status = B_USB_RAW_STATUS_FAILED;
				command.transfer.length = 0;
				status = B_OK;
				break;
			}

			status = result.Wait();
			if (status != B_OK)
				endpointInfo->handle->CancelQueuedTransfers();

			command.transfer.status = result.status;
			command.transfer.length = result.actualLength;
			deviceLocker.Unlock();

			if (command.transfer.status == B_OK)
				status = B_OK;
			if (op == B_USB_RAW_COMMAND_ISOCHRONOUS_TRANSFER) {
				if (user_memcpy(command.isochronous.packet_descriptors,
						packetDescriptors, descriptorsSize) != B_OK) {
					status = B_BAD_ADDRESS;
				}
			} else {
				if (inTransfer && user_memcpy(command.transfer.data,
					transferData, command.transfer.length) != B_OK) {
					status = B_BAD_ADDRESS;
				}
			}

			break;
		}
	}

	if (user_memcpy(buffer, &command, std::min<size_t>(length, sizeof(command))) != B_OK)
		return B_BAD_ADDRESS;

	return status;
}


const usb_configuration_info*
UsbDevFsNode::GetConfiguration(uint32 configIndex, status_t* status) const
{
	const usb_configuration_info* result = fDevice->GetNthConfiguration(configIndex);
	if (result == NULL) {
		*status = B_USB_RAW_STATUS_INVALID_CONFIGURATION;
		return NULL;
	}

	return result;
}


const usb_interface_info*
UsbDevFsNode::GetInterface(uint32 configIndex,
	uint32 interfaceIndex, uint32 alternateIndex, status_t* status) const
{
	const usb_configuration_info* configurationInfo
		= GetConfiguration(configIndex, status);
	if (configurationInfo == NULL)
		return NULL;

	if (interfaceIndex >= configurationInfo->interface_count) {
		*status = B_USB_RAW_STATUS_INVALID_INTERFACE;
		return NULL;
	}

	const usb_interface_info* result = NULL;
	if (alternateIndex == B_USB_RAW_ACTIVE_ALTERNATE)
		result = configurationInfo->interface[interfaceIndex].active;
	else {
		const usb_interface_list* interfaceList =
			&configurationInfo->interface[interfaceIndex];
		if (alternateIndex >= interfaceList->alt_count) {
			*status = B_USB_RAW_STATUS_INVALID_INTERFACE;
			return NULL;
		}

		result = &interfaceList->alt[alternateIndex];
	}

	return result;
}
