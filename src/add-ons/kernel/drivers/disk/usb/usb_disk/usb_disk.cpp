/*
 * Copyright 2008-2023, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Augustin Cavalier <waddlesplash>
 */


#include "usb_disk.h"

#include <stdio.h>
#include <new>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>

#include <kernel.h>
#include <syscall_restart.h>
#include <fs/devfs.h>

#include "icons.h"


struct {
	const char *vendor;
	const char *product;
	device_icon *icon;
	const char *name;
} kIconMatches[] = {
	// matches for Hama USB 2.0 Card Reader 35 in 1
	// vendor: "Transcend Information, Inc."
	// product: "63-in-1 Multi-Card Reader/Writer" ver. 0100
	// which report things like "Generic " "USB  CF Reader  "
//	{ NULL, " CF Reader", &kCFIconData, "devices/drive-removable-media-flash" },
	{ NULL, " SD Reader", &kSDIconData, "devices/drive-removable-media-flash" },
	{ NULL, " MS Reader", &kMSIconData, "devices/drive-removable-media-flash" },
//	{ NULL, " SM Reader", &kSMIconData, "devices/drive-removable-media-flash" },
	// match for my Kazam mobile phone
	// stupid thing says "MEDIATEK" " FLASH DISK     " even for internal memory
	{ "MEDIATEK", NULL, &kMobileIconData,
		"devices/drive-removable-media-flash" },
	{ NULL, NULL, NULL, NULL }
};


#define MAX_IO_BLOCKS					(128)

#define USB_DISK_DRIVER_MODULE_NAME "drivers/disk/usb_disk/driver/v1"

#define DEVICE_NAME_BASE	"disk/usb/"
#define DEVICE_NAME			DEVICE_NAME_BASE "%" B_PRIu32 "/%d/raw"


static inline void
normalize_name(char *name, size_t nameLength)
{
	bool wasSpace = false;
	size_t insertIndex = 0;
	for (size_t i = 0; i < nameLength; i++) {
		bool isSpace = name[i] == ' ';
		if (isSpace && wasSpace)
			continue;

		name[insertIndex++] = name[i];
		wasSpace = isSpace;
	}

	if (insertIndex > 0 && name[insertIndex - 1] == ' ')
		insertIndex--;

	name[insertIndex] = 0;
}


static void
usb_disk_clear_halt(UsbPipe* pipe)
{
	pipe->CancelQueuedTransfers();
	pipe->GetObject()->ClearFeature(USB_FEATURE_ENDPOINT_HALT);
}


status_t
DeviceLun::Inquiry()
{
	size_t dataLength = sizeof(scsi_inquiry_6_parameter);

	uint8 commandBlock[12];
	memset(commandBlock, 0, sizeof(commandBlock));

	commandBlock[0] = SCSI_INQUIRY_6;
	commandBlock[1] = fLogicalUnitNumber << 5;
	commandBlock[2] = 0; // page code
	commandBlock[4] = dataLength;

	scsi_inquiry_6_parameter parameter;
	status_t result = B_ERROR;
	err_act action = err_act_ok;
	for (uint32 tries = 0; tries < 3; tries++) {
		result = Operation(commandBlock, 6, &parameter,
			&dataLength, true, &action);
		if (result == B_OK || (action != err_act_retry
				&& action != err_act_many_retries)) {
			break;
		}
	}
	if (result != B_OK) {
		TRACE_ALWAYS("getting inquiry data failed: %s\n", strerror(result));
		fDeviceType = B_DISK;
		fRemovable = true;
		return result;
	}

	TRACE("peripherial_device_type  0x%02x\n",
		parameter.peripherial_device_type);
	TRACE("peripherial_qualifier    0x%02x\n",
		parameter.peripherial_qualifier);
	TRACE("removable_medium         %s\n",
		parameter.removable_medium ? "yes" : "no");
	TRACE("version                  0x%02x\n", parameter.version);
	TRACE("response_data_format     0x%02x\n", parameter.response_data_format);
	TRACE_ALWAYS("vendor_identification    \"%.8s\"\n",
		parameter.vendor_identification);
	TRACE_ALWAYS("product_identification   \"%.16s\"\n",
		parameter.product_identification);
	TRACE_ALWAYS("product_revision_level   \"%.4s\"\n",
		parameter.product_revision_level);

	memcpy(fVendorName, parameter.vendor_identification,
		MIN(sizeof(fVendorName), sizeof(parameter.vendor_identification)));
	memcpy(fProductName, parameter.product_identification,
		MIN(sizeof(fProductName),
			sizeof(parameter.product_identification)));
	memcpy(fProductRevision, parameter.product_revision_level,
		MIN(sizeof(fProductRevision),
			sizeof(parameter.product_revision_level)));

	fDeviceType = parameter.peripherial_device_type; /* 1:1 mapping */
	fRemovable = (parameter.removable_medium == 1);
	return B_OK;
}


void
DeviceLun::ResetCapacity()
{
	fBlockSize = 512;
	fBlockCount = 0;
}


status_t
DeviceLun::UpdateCapacity16()
{
	size_t dataLength = sizeof(scsi_read_capacity_16_parameter);
	scsi_read_capacity_16_parameter parameter;
	status_t result = B_ERROR;
	err_act action = err_act_ok;

	uint8 commandBlock[16];
	memset(commandBlock, 0, sizeof(commandBlock));

	commandBlock[0] = SCSI_SERVICE_ACTION_IN;
	commandBlock[1] = SCSI_SAI_READ_CAPACITY_16;
	commandBlock[10] = dataLength >> 24;
	commandBlock[11] = dataLength >> 16;
	commandBlock[12] = dataLength >> 8;
	commandBlock[13] = dataLength;

	// Retry reading the capacity up to three times. The first try might only
	// yield a unit attention telling us that the device or media status
	// changed, which is more or less expected if it is the first operation
	// on the device or the device only clears the unit atention for capacity
	// reads.
	for (int32 i = 0; i < 5; i++) {
		result = Operation(commandBlock, 16, &parameter,
			&dataLength, true, &action);

		if (result == B_OK || (action != err_act_retry
				&& action != err_act_many_retries)) {
			break;
		}
	}

	if (result != B_OK) {
		TRACE_ALWAYS("failed to update capacity: %s\n", strerror(result));
		fMediaPresent = false;
		fMediaChanged = false;
		ResetCapacity();
		return result;
	}

	fMediaPresent = true;
	fMediaChanged = false;
	fBlockSize = B_BENDIAN_TO_HOST_INT32(parameter.logical_block_length);
	fPhysicalBlockSize = fBlockSize;
	fBlockCount = B_BENDIAN_TO_HOST_INT64(parameter.last_logical_block_address) + 1;
	return B_OK;
}


status_t
DeviceLun::UpdateCapacity()
{
	size_t dataLength = sizeof(scsi_read_capacity_10_parameter);
	scsi_read_capacity_10_parameter parameter;
	status_t result = B_ERROR;
	err_act action = err_act_ok;

	uint8 commandBlock[12];
	memset(commandBlock, 0, sizeof(commandBlock));

	commandBlock[0] = SCSI_READ_CAPACITY_10;
	commandBlock[1] = fLogicalUnitNumber << 5;

	// Retry reading the capacity up to three times. The first try might only
	// yield a unit attention telling us that the device or media status
	// changed, which is more or less expected if it is the first operation
	// on the device or the device only clears the unit atention for capacity
	// reads.
	for (int32 i = 0; i < 5; i++) {
		result = Operation(commandBlock, 10, &parameter,
			&dataLength, true, &action);

		if (result == B_OK || (action != err_act_retry
				&& action != err_act_many_retries)) {
			break;
		}

		// In some cases, it's best to wait a little for the device to settle
		// before retrying.
		if (fDevice->fIsUfi && (result == B_DEV_NO_MEDIA
				|| result == B_TIMED_OUT || result == B_DEV_STALLED))
			snooze(10000);
	}

	if (result != B_OK) {
		TRACE_ALWAYS("failed to update capacity: %s\n", strerror(result));
		fMediaPresent = false;
		fMediaChanged = false;
		ResetCapacity();
		return result;
	}

	fMediaPresent = true;
	fMediaChanged = false;
	fBlockSize = B_BENDIAN_TO_HOST_INT32(parameter.logical_block_length);
	fPhysicalBlockSize = fBlockSize;
	fBlockCount = B_BENDIAN_TO_HOST_INT32(parameter.last_logical_block_address) + 1;
	if (fBlockCount == 0) {
		// try SCSI_READ_CAPACITY_16
		result = UpdateCapacity16();
		if (result != B_OK)
			return result;
	}

	fDmaResource.SetTo(new(std::nothrow) DMAResource);
	if (!fDmaResource.IsSet())
		return B_NO_MEMORY;

	dma_restrictions restrictions {
		.max_transfer_size = fBlockSize * MAX_IO_BLOCKS
	};
	CHECK_RET(fDmaResource->Init(restrictions, fBlockSize, 1, 1));

	fIoScheduler.SetTo(new(std::nothrow) IOSchedulerSimple(fDmaResource.Get()));
	if (!fIoScheduler.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fIoScheduler->Init("usb_disk"));
	fIoScheduler->SetCallback(*static_cast<IOCallback*>(this));

	return B_OK;
}


status_t
DeviceLun::Synchronize(bool force)
{
	if (fDevice->fIsUfi) {
		// UFI use interrupt because it runs all commands immediately, and
		// tells us when its done. There is no cache involved in that case,
		// so nothing to synchronize.
		return B_UNSUPPORTED;
	}

	if (fDevice->fSyncSupport == 0) {
		// this device reported an illegal request when syncing or repeatedly
		// returned an other error, it apparently does not support syncing...
		return B_UNSUPPORTED;
	}

	if (!fShouldSync && !force)
		return B_OK;

	uint8 commandBlock[12];
	memset(commandBlock, 0, sizeof(commandBlock));

	commandBlock[0] = SCSI_SYNCHRONIZE_CACHE_10;
	commandBlock[1] = fLogicalUnitNumber << 5;

	status_t result = Operation(commandBlock, 10, NULL, NULL, false);

	if (result == B_OK) {
		fDevice->fSyncSupport = SYNC_SUPPORT_RELOAD;
		fShouldSync = false;
		return B_OK;
	}

	if (result == B_DEV_INVALID_IOCTL)
		fDevice->fSyncSupport = 0;
	else
		fDevice->fSyncSupport--;

	return result;
}


status_t
DeviceLun::SendDiagnostic()
{
	uint8 commandBlock[12];
	memset(commandBlock, 0, sizeof(commandBlock));

	commandBlock[0] = SCSI_SEND_DIAGNOSTIC;
	commandBlock[1] = (fLogicalUnitNumber << 5) | 4;

	status_t result = Operation(commandBlock, 6, NULL, NULL, false);

	int retry = 100;
	err_act action = err_act_ok;
	while (result == B_DEV_NO_MEDIA && retry > 0) {
		snooze(10000);
		result = RequestSense(&action);
		retry--;
	}

	if (result != B_OK)
		TRACE("Send Diagnostic failed: %s\n", strerror(result));
	return result;
}


status_t
DeviceLun::TestUnitReady(err_act* _action)
{
	// if unsupported we assume the unit is fixed and therefore always ok
	if (fDevice->fIsUfi || !fDevice->fTurSupported)
		return B_OK;

	status_t result = B_OK;
	uint8 commandBlock[12] {};

	if (fDevice->fIsAtapi) {
		commandBlock[0] = SCSI_START_STOP_UNIT_6;
		commandBlock[1] = fLogicalUnitNumber << 5;
		commandBlock[2] = 0;
		commandBlock[3] = 0;
		commandBlock[4] = 1;

		result = Operation(commandBlock, 6, NULL, NULL, false, _action);
	} else {
		commandBlock[0] = SCSI_TEST_UNIT_READY_6;
		commandBlock[1] = fLogicalUnitNumber << 5;
		commandBlock[2] = 0;
		commandBlock[3] = 0;
		commandBlock[4] = 0;
		result = Operation(commandBlock, 6, NULL, NULL, true, _action);
	}

	if (result == B_DEV_INVALID_IOCTL) {
		fDevice->fTurSupported = false;
		return B_OK;
	}

	return result;
}


status_t
DeviceLun::OperationInterrupt(uint8* operation, const transfer_data& data, size_t *dataLength, bool directionIn, err_act *_action)
{
	TRACE("operation: lun: %u; op: 0x%x; data: %p; dlen: %p (%lu); in: %c\n",
		fLogicalUnitNumber, operation[0], data.vecs, dataLength,
		dataLength ? *dataLength : 0, directionIn ? 'y' : 'n');
	ASSERT_LOCKED_RECURSIVE(&fDevice->fIoLock);

	// Step 1 : send the SCSI operation as a class specific request
	size_t actualLength = 12;
	status_t result = fDevice->fInterface->SendRequest(
		USB_REQTYPE_CLASS | USB_REQTYPE_INTERFACE_OUT, 0 /*request*/,
		0/*value*/, 12, operation, &actualLength);

	if (result != B_OK || actualLength != 12) {
		TRACE("Command stage: wrote %ld bytes (error: %s)\n",
			actualLength, strerror(result));

		// There was an error, we have to do a request sense to reset the device
		if (operation[0] != SCSI_REQUEST_SENSE_6) {
			RequestSense(_action);
		}
		return result;
	}

	// Step 2 : data phase : send or receive data
	size_t transferedData = 0;
	if (data.vec_count != 0) {
		// we have data to transfer in a data stage
		result = fDevice->TransferData(directionIn, data);
		if (result != B_OK) {
			TRACE("Error %s in data phase\n", strerror(result));
			return result;
		}

		transferedData = fDevice->fActualLength;
		if (fDevice->fStatus != B_OK || transferedData != *dataLength) {
			// sending or receiving of the data failed
			if (fDevice->fStatus == B_DEV_STALLED) {
				TRACE("stall while transfering data\n");
				usb_disk_clear_halt(directionIn ? fDevice->fBulkIn : fDevice->fBulkOut);
			} else {
				TRACE_ALWAYS("sending or receiving of the data failed\n");
				fDevice->ResetRecovery(_action);
				return B_IO_ERROR;
			}
		}
	}

	// step 3 : wait for the device to send the interrupt ACK
	if (operation[0] != SCSI_REQUEST_SENSE_6) {
		interrupt_status_wrapper status;
		result = fDevice->ReceiveCswInterrupt(&status);
		if (result != B_OK) {
			// in case of a stall or error clear the stall and try again
			TRACE("Error receiving interrupt: %s. Retrying...\n",
				strerror(result));
			usb_disk_clear_halt(fDevice->fBulkIn);
			result = fDevice->ReceiveCswInterrupt(&status);
		}

		if (result != B_OK) {
			TRACE_ALWAYS("receiving the command status interrupt failed\n");
			fDevice->ResetRecovery(_action);
			return result;
		}

		// wait for the device to finish the operation.
		result = RequestSense(_action);
	}
	return result;
}


status_t
DeviceLun::OperationBulk(uint8 *operation, size_t operationLength, const transfer_data& data, size_t *dataLength, bool directionIn, err_act *_action)
{
	TRACE("operation: lun: %u; op: %u; data: %p; dlen: %p (%lu); in: %c\n",
		fLogicalUnitNumber, operation[0],
		data.vecs, dataLength, dataLength ? *dataLength : 0,
		directionIn ? 'y' : 'n');
	ASSERT_LOCKED_RECURSIVE(&fDevice->fIoLock);

	usb_massbulk_command_block_wrapper command;
	command.signature = USB_MASSBULK_CBW_SIGNATURE;
	command.tag = fDevice->fCurrentTag++;
	command.data_transfer_length = (dataLength != NULL ? *dataLength : 0);
	command.flags = (directionIn ? USB_MASSBULK_CBW_DATA_INPUT
		: USB_MASSBULK_CBW_DATA_OUTPUT);
	command.lun = fLogicalUnitNumber;
	command.command_block_length
		= fDevice->fIsAtapi ? ATAPI_COMMAND_LENGTH : operationLength;
	memset(command.command_block, 0, sizeof(command.command_block));
	memcpy(command.command_block, operation, operationLength);

	status_t result = fDevice->TransferData(false, &command,
		sizeof(usb_massbulk_command_block_wrapper));
	if (result != B_OK)
		return result;

	if (fDevice->fStatus != B_OK ||
		fDevice->fActualLength != sizeof(usb_massbulk_command_block_wrapper)) {
		// sending the command block wrapper failed
		TRACE_ALWAYS("sending the command block wrapper failed: %s\n",
			strerror(fDevice->fStatus));
		fDevice->ResetRecovery(_action);
		return B_IO_ERROR;
	}

	size_t transferedData = 0;
	if (data.vec_count != 0) {
		// we have data to transfer in a data stage
		result = fDevice->TransferData(directionIn, data);
		if (result != B_OK)
			return result;

		transferedData = fDevice->fActualLength;
		if (fDevice->fStatus != B_OK || transferedData != *dataLength) {
			// sending or receiving of the data failed
			if (fDevice->fStatus == B_DEV_STALLED) {
				TRACE("stall while transfering data\n");
				usb_disk_clear_halt(directionIn ? fDevice->fBulkIn : fDevice->fBulkOut);
			} else {
				TRACE_ALWAYS("sending or receiving of the data failed: %s\n",
					strerror(fDevice->fStatus));
				fDevice->ResetRecovery(_action);
				return B_IO_ERROR;
			}
		}
	}

	usb_massbulk_command_status_wrapper status;
	result = fDevice->ReceiveCswBulk(&status);
	if (result != B_OK) {
		// in case of a stall or error clear the stall and try again
		usb_disk_clear_halt(fDevice->fBulkIn);
		result = fDevice->ReceiveCswBulk(&status);
	}

	if (result != B_OK) {
		TRACE_ALWAYS("receiving the command status wrapper failed: %s\n",
			strerror(result));
		fDevice->ResetRecovery(_action);
		return result;
	}

	if (status.signature != USB_MASSBULK_CSW_SIGNATURE
		|| status.tag != command.tag) {
		// the command status wrapper is not valid
		TRACE_ALWAYS("command status wrapper is not valid: %#" B_PRIx32 "\n",
			status.signature);
		fDevice->ResetRecovery(_action);
		return B_ERROR;
	}

	switch (status.status) {
		case USB_MASSBULK_CSW_STATUS_COMMAND_PASSED:
		case USB_MASSBULK_CSW_STATUS_COMMAND_FAILED:
		{
			// The residue from "status.data_residue" is not maintained
			// correctly by some devices, so calculate it instead.
			uint32 residue = command.data_transfer_length - transferedData;

			if (dataLength != NULL) {
				*dataLength -= residue;
				if (transferedData < *dataLength) {
					TRACE_ALWAYS("less data transfered than indicated: %"
						B_PRIuSIZE " vs. %" B_PRIuSIZE "\n", transferedData,
						*dataLength);
					*dataLength = transferedData;
				}
			}

			if (status.status == USB_MASSBULK_CSW_STATUS_COMMAND_PASSED) {
				// the operation is complete and has succeeded
				return B_OK;
			} else {
				if (operation[0] == SCSI_REQUEST_SENSE_6)
					return B_ERROR;

				// the operation is complete but has failed at the SCSI level
				if (operation[0] != SCSI_TEST_UNIT_READY_6) {
					TRACE_ALWAYS("operation %#" B_PRIx8
						" failed at the SCSI level\n", operation[0]);
				}

				result = RequestSense(_action);
				return result == B_OK ? B_ERROR : result;
			}
		}

		case USB_MASSBULK_CSW_STATUS_PHASE_ERROR:
		{
			// a protocol or device error occured
			TRACE_ALWAYS("phase error in operation %#" B_PRIx8 "\n",
				operation[0]);
			fDevice->ResetRecovery(_action);
			return B_ERROR;
		}

		default:
		{
			// command status wrapper is not meaningful
			TRACE_ALWAYS("command status wrapper has invalid status\n");
			fDevice->ResetRecovery(_action);
			return B_ERROR;
		}
	}
}


status_t
DeviceLun::Operation(uint8* operation, size_t opLength, const transfer_data& data, size_t *dataLength, bool directionIn, err_act *_action)
{
	if (fDevice->fIsUfi)
		return OperationInterrupt(operation, data, dataLength, directionIn, _action);
	else
		return OperationBulk(operation, opLength, data, dataLength, directionIn, _action);
}


status_t
DeviceLun::Operation(uint8* operation, size_t opLength, void *buffer, size_t *dataLength, bool directionIn, err_act *_action)
{
	iovec vec;
	vec.iov_base = buffer;

	struct transfer_data data;
	data.vecs = &vec;

	if (dataLength != NULL && *dataLength != 0) {
		vec.iov_len = *dataLength;
		data.vec_count = 1;
	} else {
		vec.iov_len = 0;
		data.vec_count = 0;
	}

	return Operation(operation, opLength, data, dataLength, directionIn, _action);
}


status_t
DeviceLun::RequestSense(err_act *_action)
{
	size_t dataLength = sizeof(scsi_request_sense_6_parameter);
	uint8 commandBlock[12];
	memset(commandBlock, 0, sizeof(commandBlock));

	commandBlock[0] = SCSI_REQUEST_SENSE_6;
	commandBlock[1] = fLogicalUnitNumber << 5;
	commandBlock[2] = 0; // page code
	commandBlock[4] = dataLength;

	scsi_request_sense_6_parameter parameter;
	status_t result = B_ERROR;
	for (uint32 tries = 0; tries < 3; tries++) {
		result = Operation(commandBlock, 6, &parameter,
			&dataLength, true);
		if (result != B_TIMED_OUT)
			break;
		snooze(100000);
	}
	if (result != B_OK) {
		TRACE_ALWAYS("getting request sense data failed: %s\n",
			strerror(result));
		return result;
	}

	const char *label = NULL;
	err_act action = err_act_fail;
	status_t status = B_ERROR;
	scsi_get_sense_asc_info((parameter.additional_sense_code << 8)
		| parameter.additional_sense_code_qualifier, &label, &action,
		&status);

	if (parameter.sense_key > SCSI_SENSE_KEY_NOT_READY
		&& parameter.sense_key != SCSI_SENSE_KEY_UNIT_ATTENTION) {
		TRACE_ALWAYS("request_sense: key: 0x%02x; asc: 0x%02x; ascq: "
			"0x%02x; %s\n", parameter.sense_key,
			parameter.additional_sense_code,
			parameter.additional_sense_code_qualifier,
			label ? label : "(unknown)");
	}

	if ((parameter.additional_sense_code == 0
			&& parameter.additional_sense_code_qualifier == 0)
		|| label == NULL) {
		scsi_get_sense_key_info(parameter.sense_key, &label, &action, &status);
	}

	if (status == B_DEV_MEDIA_CHANGED) {
		fMediaChanged = true;
		fMediaPresent = true;
	} else if (parameter.sense_key == SCSI_SENSE_KEY_UNIT_ATTENTION
		&& status != B_DEV_NO_MEDIA) {
		fMediaPresent = true;
	} else if (status == B_DEV_NOT_READY) {
		fMediaPresent = false;
		ResetCapacity();
	}

	if (_action != NULL)
		*_action = action;

	return status;
}


status_t
DeviceLun::HandleMediaChange(MutexLocker& locker)
{
	RecursiveLocker ioLocker;
	status_t result = fDevice->AcquireIoLock(locker, ioLocker);
	if (result != B_OK)
		return result;

	// It may have been handled while we were waiting for locks.
	if (fMediaChanged) {
		result = UpdateCapacity();
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


status_t
DeviceLun::BlockRead(uint64 blockPosition, size_t blockCount, struct transfer_data data, size_t *length)
{
	uint8 commandBlock[16];
	memset(commandBlock, 0, sizeof(commandBlock));
	if (fDevice->fIsUfi) {
		commandBlock[0] = SCSI_READ_12;
		commandBlock[1] = fLogicalUnitNumber << 5;
		commandBlock[2] = blockPosition >> 24;
		commandBlock[3] = blockPosition >> 16;
		commandBlock[4] = blockPosition >> 8;
		commandBlock[5] = blockPosition;
		commandBlock[6] = blockCount >> 24;
		commandBlock[7] = blockCount >> 16;
		commandBlock[8] = blockCount >> 8;
		commandBlock[9] = blockCount;

		status_t result = B_OK;
		for (int tries = 0; tries < 5; tries++) {
			result = Operation(commandBlock, 12, data, length, true);
			if (result == B_OK)
				break;
			else
				snooze(10000);
		}
		return result;
	} else if (blockPosition + blockCount < 0x100000000LL && blockCount <= 0x10000) {
		commandBlock[0] = SCSI_READ_10;
		commandBlock[1] = 0;
		commandBlock[2] = blockPosition >> 24;
		commandBlock[3] = blockPosition >> 16;
		commandBlock[4] = blockPosition >> 8;
		commandBlock[5] = blockPosition;
		commandBlock[7] = blockCount >> 8;
		commandBlock[8] = blockCount;
		return Operation(commandBlock, 10, data, length, true);
	} else {
		commandBlock[0] = SCSI_READ_16;
		commandBlock[1] = 0;
		commandBlock[2] = blockPosition >> 56;
		commandBlock[3] = blockPosition >> 48;
		commandBlock[4] = blockPosition >> 40;
		commandBlock[5] = blockPosition >> 32;
		commandBlock[6] = blockPosition >> 24;
		commandBlock[7] = blockPosition >> 16;
		commandBlock[8] = blockPosition >> 8;
		commandBlock[9] = blockPosition;
		commandBlock[10] = blockCount >> 24;
		commandBlock[11] = blockCount >> 16;
		commandBlock[12] = blockCount >> 8;
		commandBlock[13] = blockCount;
		return Operation(commandBlock, 16, data, length, true);
	}
}


status_t
DeviceLun::BlockWrite(uint64 blockPosition, size_t blockCount, struct transfer_data data, size_t *length)
{
	uint8 commandBlock[16];
	memset(commandBlock, 0, sizeof(commandBlock));

	if (fDevice->fIsUfi) {
		commandBlock[0] = SCSI_WRITE_12;
		commandBlock[1] = fLogicalUnitNumber << 5;
		commandBlock[2] = blockPosition >> 24;
		commandBlock[3] = blockPosition >> 16;
		commandBlock[4] = blockPosition >> 8;
		commandBlock[5] = blockPosition;
		commandBlock[6] = blockCount >> 24;
		commandBlock[7] = blockCount >> 16;
		commandBlock[8] = blockCount >> 8;
		commandBlock[9] = blockCount;

		status_t result;
		result = Operation(commandBlock, 12, data, length, false);

		int retry = 10;
		err_act action = err_act_ok;
		while (result == B_DEV_NO_MEDIA && retry > 0) {
			snooze(10000);
			result = RequestSense(&action);
			retry--;
		}

		if (result == B_OK)
			fShouldSync = true;
		return result;
	} else if (blockPosition + blockCount < 0x100000000LL && blockCount <= 0x10000) {
		commandBlock[0] = SCSI_WRITE_10;
		commandBlock[2] = blockPosition >> 24;
		commandBlock[3] = blockPosition >> 16;
		commandBlock[4] = blockPosition >> 8;
		commandBlock[5] = blockPosition;
		commandBlock[7] = blockCount >> 8;
		commandBlock[8] = blockCount;
		status_t result = Operation(commandBlock, 10, data, length, false);
		if (result == B_OK)
			fShouldSync = true;
		return result;
	} else {
		commandBlock[0] = SCSI_WRITE_16;
		commandBlock[1] = 0;
		commandBlock[2] = blockPosition >> 56;
		commandBlock[3] = blockPosition >> 48;
		commandBlock[4] = blockPosition >> 40;
		commandBlock[5] = blockPosition >> 32;
		commandBlock[6] = blockPosition >> 24;
		commandBlock[7] = blockPosition >> 16;
		commandBlock[8] = blockPosition >> 8;
		commandBlock[9] = blockPosition;
		commandBlock[10] = blockCount >> 24;
		commandBlock[11] = blockCount >> 16;
		commandBlock[12] = blockCount >> 8;
		commandBlock[13] = blockCount;
		status_t result = Operation(commandBlock, 16, data, length, false);
		if (result == B_OK)
			fShouldSync = true;
		return result;
	}
}


status_t
DeviceLun::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	CALLED();

	MutexLocker locker(fDevice->fLock);

	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
DeviceLun::Close()
{
	RecursiveLocker ioLocker(fDevice->fIoLock);
	MutexLocker deviceLocker(fDevice->fLock);

	if (!fDevice->fRemoved)
		Synchronize(false);

	return B_OK;
}


status_t
DeviceLun::IO(io_request *request)
{
	return fIoScheduler->ScheduleRequest(request);
}


status_t
DeviceLun::Control(uint32 op, void *buffer, size_t length)
{
	MutexLocker locker(&fDevice->fLock);
	if (fDevice->fRemoved)
		return B_DEV_NOT_READY;
	RecursiveLocker ioLocker;

	switch (op) {
		case B_GET_DEVICE_SIZE:
		{
			if (fMediaChanged) {
				status_t result = HandleMediaChange(locker);
				if (result != B_OK)
					return result;
			}

			size_t size = fBlockSize * fBlockCount;
			return user_memcpy(buffer, &size, sizeof(size));
		}

		case B_GET_MEDIA_STATUS:
		{
			status_t result = fDevice->AcquireIoLock(locker, ioLocker);
			if (result != B_OK)
				return result;

			err_act action = err_act_ok;
			status_t ready;
			for (uint32 tries = 0; tries < 3; tries++) {
				ready = TestUnitReady(&action);
				if (ready == B_OK || ready == B_DEV_NO_MEDIA
					|| (action != err_act_retry
						&& action != err_act_many_retries)) {
					if (IS_USER_ADDRESS(buffer)) {
						if (user_memcpy(buffer, &ready, sizeof(status_t)) != B_OK)
							return B_BAD_ADDRESS;
					} else if (is_called_via_syscall()) {
						return B_BAD_ADDRESS;
					} else
						*(status_t *)buffer = ready;
					break;
				}
				snooze(500000);
			}
			TRACE("B_GET_MEDIA_STATUS: 0x%08" B_PRIx32 "\n", ready);
			return B_OK;
		}

		case B_GET_GEOMETRY:
		{
			if (buffer == NULL || length > sizeof(device_geometry))
				return B_BAD_VALUE;
			if (fMediaChanged) {
				status_t result = HandleMediaChange(locker);
				if (result != B_OK)
					return result;
			}

			device_geometry geometry;
			devfs_compute_geometry_size(&geometry, fBlockCount, fBlockSize);
			geometry.bytes_per_physical_sector = fPhysicalBlockSize;

			geometry.device_type = fDeviceType;
			geometry.removable = fRemovable;
			geometry.read_only = fWriteProtected;
			geometry.write_once = fDeviceType == B_WORM;
			TRACE("B_GET_GEOMETRY: %" B_PRId32 " sectors at %" B_PRId32
				" bytes per sector\n", geometry.cylinder_count,
				geometry.bytes_per_sector);
			return user_memcpy(buffer, &geometry, length);
		}

		case B_FLUSH_DRIVE_CACHE:
		{
			TRACE("B_FLUSH_DRIVE_CACHE\n");

			status_t result = fDevice->AcquireIoLock(locker, ioLocker);
			if (result != B_OK)
				return result;

			return Synchronize(true);
		}

		case B_EJECT_DEVICE:
		{
			status_t result = fDevice->AcquireIoLock(locker, ioLocker);
			if (result != B_OK)
				return result;

			uint8 commandBlock[12];
			memset(commandBlock, 0, sizeof(commandBlock));

			commandBlock[0] = SCSI_START_STOP_UNIT_6;
			commandBlock[1] = fLogicalUnitNumber << 5;
			commandBlock[4] = 2;

			return Operation(commandBlock, 6, NULL, NULL, false);
		}

		case B_LOAD_MEDIA:
		{
			status_t result = fDevice->AcquireIoLock(locker, ioLocker);
			if (result != B_OK)
				return result;

			uint8 commandBlock[12];
			memset(commandBlock, 0, sizeof(commandBlock));

			commandBlock[0] = SCSI_START_STOP_UNIT_6;
			commandBlock[1] = fLogicalUnitNumber << 5;
			commandBlock[4] = 3;

			return Operation(commandBlock, 6, NULL, NULL, false);
		}

		case B_GET_ICON:
			// We don't support this legacy ioctl anymore, but the two other
			// icon ioctls below instead.
			break;

		case B_GET_ICON_NAME:
		{
			const char *iconName = "devices/drive-removable-media-usb";
			char vendor[sizeof(fVendorName)+1];
			char product[sizeof(fProductName)+1];

			if (fDevice->fIsUfi) {
				iconName = "devices/drive-floppy-usb";
			}

			switch (fDeviceType) {
				case B_CD:
				case B_OPTICAL:
					iconName = "devices/drive-optical";
					break;
				case B_TAPE:	// TODO
				default:
					snprintf(vendor, sizeof(vendor), "%.8s",
						fVendorName);
					snprintf(product, sizeof(product), "%.16s",
						fProductName);
					for (int i = 0; kIconMatches[i].icon; i++) {
						if (kIconMatches[i].vendor != NULL
							&& strstr(vendor, kIconMatches[i].vendor) == NULL)
							continue;
						if (kIconMatches[i].product != NULL
							&& strstr(product, kIconMatches[i].product) == NULL)
							continue;
						iconName = kIconMatches[i].name;
					}
					break;
			}
			return user_strlcpy((char *)buffer, iconName,
				B_FILE_NAME_LENGTH);
		}

		case B_GET_VECTOR_ICON:
		{
			device_icon *icon = &kKeyIconData;
			char vendor[sizeof(fVendorName)+1];
			char product[sizeof(fProductName)+1];

			if (length != sizeof(device_icon))
				return B_BAD_VALUE;

			if (fDevice->fIsUfi) {
				// UFI is specific for floppy drives
				icon = &kFloppyIconData;
			} else {
				switch (fDeviceType) {
					case B_CD:
					case B_OPTICAL:
						icon = &kCDIconData;
						break;
					case B_TAPE:	// TODO
					default:
						snprintf(vendor, sizeof(vendor), "%.8s",
								fVendorName);
						snprintf(product, sizeof(product), "%.16s",
								fProductName);
						for (int i = 0; kIconMatches[i].icon; i++) {
							if (kIconMatches[i].vendor != NULL
									&& strstr(vendor,
										kIconMatches[i].vendor) == NULL)
								continue;
							if (kIconMatches[i].product != NULL
									&& strstr(product,
										kIconMatches[i].product) == NULL)
								continue;
							icon = kIconMatches[i].icon;
						}
						break;
				}
			}

			device_icon iconData;
			if (user_memcpy(&iconData, buffer, sizeof(device_icon)) != B_OK)
				return B_BAD_ADDRESS;

			if (iconData.icon_size >= icon->icon_size) {
				if (user_memcpy(iconData.icon_data, icon->icon_data,
						(size_t)icon->icon_size) != B_OK)
					return B_BAD_ADDRESS;
			}

			iconData.icon_size = icon->icon_size;
			return user_memcpy(buffer, &iconData, sizeof(device_icon));
		}

		case B_GET_DEVICE_NAME:
		{
			size_t nameLength = sizeof(fVendorName)
				+ sizeof(fProductName) + sizeof(fProductRevision) + 3;

			char name[nameLength];
			snprintf(name, nameLength, "%.8s %.16s %.4s", fVendorName,
				fProductName, fProductRevision);

			normalize_name(name, nameLength);

			status_t result = user_strlcpy((char *)buffer, name, length);
			if (result > 0)
				result = B_OK;

			TRACE_ALWAYS("got device name \"%s\": %s\n", name,
				strerror(result));
			return result;
		}
	}

	TRACE_ALWAYS("unhandled ioctl %" B_PRId32 "\n", op);
	return B_DEV_INVALID_IOCTL;
}


status_t
DeviceLun::DoIO(IOOperation* operation)
{
	TRACE("IOO offset: %" B_PRIdOFF ", length: %" B_PRIuGENADDR
		", write: %s\n", operation->Offset(),
		operation->Length(), operation->IsWrite() ? "yes" : "no");

	RecursiveLocker ioLocker(fDevice->fIoLock);
	MutexLocker deviceLocker(fDevice->fLock);

	status_t status = B_OK;
	if (fDevice->fRemoved)
		status = B_DEV_NOT_READY;

	struct transfer_data data;
	data.physical = true;
	data.phys_vecs = (physical_entry*)operation->Vecs();
	data.vec_count = operation->VecCount();

	size_t length = operation->Length();
	const uint64 blockPosition = operation->Offset() / fBlockSize;
	const size_t blockCount = length / fBlockSize;

	if (status >= B_OK) {
		if (operation->IsWrite())
			status = BlockWrite(blockPosition, blockCount, data, &length);
		else
			status = BlockRead(blockPosition, blockCount, data, &length);
	}

	fIoScheduler->OperationCompleted(operation, status, status < B_OK ? 0 : length);

	return status;
}


// #pragma mark - UsbDiskDriver

status_t
UsbDiskDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<UsbDiskDriver> driver(new(std::nothrow) UsbDiskDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t UsbDiskDriver::Init()
{
	CALLED();

	fDevice = fNode->QueryBusInterface<UsbDevice>();


	recursive_lock_lock(&fIoLock);
	mutex_lock(&fLock);

	const usb_configuration_info* configuration = fDevice->GetConfiguration();
	if (configuration == NULL)
		return B_ERROR;

	for (size_t i = 0; i < configuration->interface_count; i++) {
		usb_interface_info *interface = configuration->interface[i].active;
		if (interface == NULL)
			continue;

		if (interface->descr->interface_class == USB_MASS_STORAGE_DEVICE_CLASS
			&& (((interface->descr->interface_subclass == 0x06 /* SCSI */
					|| interface->descr->interface_subclass == 0x02 /* ATAPI */
					|| interface->descr->interface_subclass == 0x05 /* ATAPI */)
				&& interface->descr->interface_protocol == 0x50 /* bulk-only */)
			|| (interface->descr->interface_subclass == 0x04 /* UFI */
				&& interface->descr->interface_protocol == 0x00))) {

			bool hasIn = false;
			bool hasOut = false;
			bool hasInt = false;
			for (size_t j = 0; j < interface->endpoint_count; j++) {
				usb_endpoint_info *endpoint = &interface->endpoint[j];
				if (endpoint == NULL)
					continue;

				if (!hasIn && (endpoint->descr->endpoint_address
					& USB_ENDPOINT_ADDR_DIR_IN) != 0
					&& endpoint->descr->attributes == USB_ENDPOINT_ATTR_BULK) {
					fBulkIn = endpoint->handle;
					hasIn = true;
				} else if (!hasOut && (endpoint->descr->endpoint_address
					& USB_ENDPOINT_ADDR_DIR_IN) == 0
					&& endpoint->descr->attributes == USB_ENDPOINT_ATTR_BULK) {
					fBulkOut = endpoint->handle;
					hasOut = true;
				} else if (!hasInt && (endpoint->descr->endpoint_address
					& USB_ENDPOINT_ADDR_DIR_IN)
					&& endpoint->descr->attributes
					== USB_ENDPOINT_ATTR_INTERRUPT) {
					fInterrupt = endpoint->handle;
					hasInt = true;
				}

				if (hasIn && hasOut && hasInt)
					break;
			}

			if (!(hasIn && hasOut)) {
				// Missing one of the required endpoints, try next interface
				continue;
			}

			fInterface = interface->handle;
			fIsAtapi = interface->descr->interface_subclass != 0x06
				&& interface->descr->interface_subclass != 0x04;
			fIsUfi = interface->descr->interface_subclass == 0x04;

			if (fIsUfi && !hasInt) {
				// UFI without interrupt endpoint is not possible.
				continue;
			}
			break;
		}
	}

	if (fInterface == NULL) {
		TRACE_ALWAYS("no valid bulk-only or CBI interface found\n");
		return B_ERROR;
	}

	fNotify.SetTo(create_sem(0, "usb_disk callback notify"));
	CHECK_RET(fNotify.Get());

	if (fIsUfi) {
		fInterruptLock.SetTo(create_sem(0, "usb_disk interrupt lock"));
		CHECK_RET(fInterruptLock.Get());
	}

	fLunCount = GetMaxLun() + 1;
	fLuns.SetTo(new(std::nothrow) DeviceLun[fLunCount]);
	if (!fLuns.IsSet())
		return B_NO_MEMORY;

	TRACE_ALWAYS("device reports a lun count of %d\n", fLunCount);

	for (uint8 i = 0; i < fLunCount; i++) {
		// create the individual luns present on this device
		DeviceLun* lun = &fLuns[i];

		lun->fDevice = this;
		lun->fLogicalUnitNumber = i;
		lun->fShouldSync = false;
		lun->fMediaPresent = true;
		lun->fMediaChanged = true;

		memset(lun->fVendorName, 0, sizeof(lun->fVendorName));
		memset(lun->fProductName, 0, sizeof(lun->fProductName));
		memset(lun->fProductRevision, 0, sizeof(lun->fProductRevision));

		lun->ResetCapacity();

		// initialize this lun
		CHECK_RET(lun->Inquiry());

		if (fIsUfi) {
			// Reset the device
			// If we don't do it all the other commands except inquiry and send
			// diagnostics will be stalled.
			CHECK_RET(lun->SendDiagnostic());
		}

		err_act action = err_act_ok;
		for (uint32 tries = 0; tries < 8; tries++) {
			TRACE("usb lun %" B_PRIu8 " inquiry attempt %" B_PRIu32 " begin\n",
				i, tries);
			status_t ready = lun->TestUnitReady(&action);
			if (ready == B_OK || ready == B_DEV_NO_MEDIA
				|| ready == B_DEV_MEDIA_CHANGED) {
				if (lun->fDeviceType == B_CD)
					lun->fWriteProtected = true;
				// TODO: check for write protection; disabled since some
				// devices lock up when getting the mode sense
				else if (/*lun->ModeSense() != B_OK*/true)
					lun->fWriteProtected = false;

				TRACE("usb lun %" B_PRIu8 " ready. write protected = %c%s\n", i,
					lun->fWriteProtected ? 'y' : 'n',
					ready == B_DEV_NO_MEDIA ? " (no media inserted)" : "");

				break;
			}
			TRACE("usb lun %" B_PRIu8 " inquiry attempt %" B_PRIu32 " failed\n",
				i, tries);
			if (action != err_act_retry && action != err_act_many_retries)
				break;
			bigtime_t snoozeTime = 1000000 * tries;
			TRACE("snoozing %" B_PRIu64 " microseconds for usb lun\n",
				snoozeTime);
			snooze(snoozeTime);
		}
	}

	mutex_unlock(&fLock);
	recursive_lock_unlock(&fIoLock);

	TRACE("new device: 0x%p\n", this);

	// TODO: proper device ID allocation
	static int32 lastId = 0;
	fNumber = lastId++;

	for (uint8 i = 0; i < fLunCount; i++) {
		DeviceLun& lun = fLuns[i];
		sprintf(lun.fName, DEVICE_NAME, fNumber, i);
		CHECK_RET(fNode->RegisterDevFsNode(lun.fName, static_cast<DevFsNode*>(&lun)));
	}

	return B_OK;
}


void
UsbDiskDriver::DeviceRemoved()
{
	TRACE("DeviceRemoved(0x%p)\n", this);
	mutex_lock(&fLock);

	for (uint8 i = 0; i < fLunCount; i++) {
		// unpublish_device() can call close().
		mutex_unlock(&fLock);
		fNode->UnregisterDevFsNode(fLuns[i].fName);
		mutex_lock(&fLock);
	}

	fRemoved = true;
	fBulkIn->CancelQueuedTransfers();
	fBulkOut->CancelQueuedTransfers();

	mutex_unlock(&fLock);
}


uint8
UsbDiskDriver::GetMaxLun()
{
	ASSERT_LOCKED_RECURSIVE(&fIoLock);

	uint8 result = 0;
	size_t actualLength = 0;

	// devices that do not support multiple LUNs may stall this request
	if (fInterface->SendRequest(USB_REQTYPE_INTERFACE_IN
		| USB_REQTYPE_CLASS, USB_MASSBULK_REQUEST_GET_MAX_LUN, 0x0000,
		1, &result, &actualLength) != B_OK
			|| actualLength != 1) {
		return 0;
	}

	if (result > MAX_LOGICAL_UNIT_NUMBER) {
		// invalid max lun
		return 0;
	}

	return result;
}


status_t
UsbDiskDriver::MassStorageReset()
{
	return fInterface->SendRequest(USB_REQTYPE_INTERFACE_OUT
		| USB_REQTYPE_CLASS, USB_MASSBULK_REQUEST_MASS_STORAGE_RESET, 0x0000,
		0, NULL, NULL);
}


void
UsbDiskDriver::Callback(void* cookie, status_t status, void* data, size_t actualLength)
{
	//TRACE("callback()\n");
	UsbDiskDriver* device = (UsbDiskDriver*)cookie;
	device->fStatus = status;
	device->fActualLength = actualLength;
	release_sem(device->fNotify.Get());
}


void
UsbDiskDriver::CallbackInterrupt(void* cookie, int32 status, void* data, size_t length)
{
	UsbDiskDriver* device = (UsbDiskDriver*)cookie;
	// We release the lock even if the interrupt is invalid. This way there
	// is at least a chance for the driver to terminate properly.
	release_sem(device->fInterruptLock.Get());

	if (length != 2) {
		TRACE_ALWAYS("interrupt of length %" B_PRIuSIZE "! (expected 2)\n",
			length);
		// In this case we do not reschedule the interrupt. This means the
		// driver will be locked. The interrupt should perhaps be scheduled
		// when starting a transfer instead. But getting there means something
		// is really broken, so...
		return;
	}

	// Reschedule the interrupt for next time
	device->fInterrupt->QueueInterrupt(device->fInterruptBuffer, 2, CallbackInterrupt, cookie);
}


void
UsbDiskDriver::ResetRecovery(err_act* _action)
{
	TRACE("reset recovery\n");
	ASSERT_LOCKED_RECURSIVE(&fIoLock);

	MassStorageReset();
	usb_disk_clear_halt(fBulkIn);
	usb_disk_clear_halt(fBulkOut);
	if (fIsUfi)
		usb_disk_clear_halt(fInterrupt);

	if (_action != NULL)
		*_action = err_act_retry;
}


status_t
UsbDiskDriver::TransferData(bool directionIn, const transfer_data& data)
{
	status_t result;
	UsbPipe* pipe = directionIn ? fBulkIn : fBulkOut;
	if (data.physical)
		result = pipe->QueueBulkVPhysical(data.phys_vecs, data.vec_count, Callback, this);
	else
		result = pipe->QueueBulkV(data.vecs, data.vec_count, Callback, this);

	if (result != B_OK) {
		TRACE_ALWAYS("failed to queue data transfer: %s\n", strerror(result));
		return result;
	}

	mutex_unlock(&fLock);
	do {
		result = acquire_sem_etc(fNotify.Get(), 1, B_RELATIVE_TIMEOUT, 10 * 1000 * 1000);
		if (result == B_TIMED_OUT) {
			// Cancel the transfer and collect the sem that should now be
			// released through the callback on cancel. Handling of device
			// reset is done in usb_disk_operation() when it detects that
			// the transfer failed.
			pipe->CancelQueuedTransfers();
			acquire_sem_etc(fNotify.Get(), 1, B_RELATIVE_TIMEOUT, 0);
		}
	} while (result == B_INTERRUPTED);
	mutex_lock(&fLock);

	if (result != B_OK) {
		TRACE_ALWAYS("acquire_sem failed while waiting for data transfer: %s\n",
			strerror(result));
		return result;
	}

	return B_OK;
}


status_t
UsbDiskDriver::TransferData(bool directionIn, void* buffer, size_t dataLength)
{
	iovec vec;
	vec.iov_base = buffer;
	vec.iov_len = dataLength;

	struct transfer_data data;
	data.vecs = &vec;
	data.vec_count = 1;

	return TransferData(directionIn, data);
}


status_t
UsbDiskDriver::ReceiveCswInterrupt(interrupt_status_wrapper* status)
{
	TRACE("Waiting for result...\n");

	fInterrupt->QueueInterrupt(fInterruptBuffer, 2, CallbackInterrupt, this);

	acquire_sem(fInterruptLock.Get());

	status->status = fInterruptBuffer[0];
	status->misc = fInterruptBuffer[1];

	return B_OK;
}


status_t
UsbDiskDriver::ReceiveCswBulk(usb_massbulk_command_status_wrapper* status)
{
	status_t result = TransferData(true, status,
		sizeof(usb_massbulk_command_status_wrapper));
	if (result != B_OK)
		return result;

	if (fStatus != B_OK
			|| fActualLength
			!= sizeof(usb_massbulk_command_status_wrapper)) {
		// receiving the command status wrapper failed
		return B_ERROR;
	}

	return B_OK;
}


status_t
UsbDiskDriver::AcquireIoLock(MutexLocker& locker, RecursiveLocker& ioLocker)
{
	locker.Unlock();
	ioLocker.SetTo(fIoLock, false, true);
	locker.Lock();

	if (!locker.IsLocked() || !ioLocker.IsLocked())
		return B_ERROR;

	if (fRemoved)
		return B_DEV_NOT_READY;

	return B_OK;
}


static driver_module_info sUsbDiskDriver = {
	.info = {
		.name = USB_DISK_DRIVER_MODULE_NAME,
	},
	.probe = UsbDiskDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sUsbDiskDriver,
	NULL
};
