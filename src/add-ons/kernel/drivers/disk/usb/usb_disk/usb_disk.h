/*
 * Copyright 2008-2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 */
#pragma once

#include <dm2/bus/USB.h>
#include <usb/USB_massbulk.h>

#include <Drivers.h>

#include <AutoDeleterOS.h>
#include <util/AutoLock.h>
#include <util/Vector.h>

#include "dma_resources.h"
#include "IORequest.h"
#include "IOSchedulerSimple.h"

#include "scsi_sense.h"
#include "usb_disk_scsi.h"


//#define TRACE_USB_DISK
#ifdef TRACE_USB_DISK
#	define TRACE(x...) dprintf("usb_disk: " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("usb_disk: " x)
#define ERROR(x...)			dprintf("\33[33musb_disk:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define REQUEST_MASS_STORAGE_RESET	0xff
#define REQUEST_GET_MAX_LUN			0xfe
#define MAX_LOGICAL_UNIT_NUMBER		15
#define ATAPI_COMMAND_LENGTH		12

#define SYNC_SUPPORT_RELOAD			5


class IOScheduler;
class DMAResource;
class UsbDiskDriver;


struct interrupt_status_wrapper {
	uint8		status;
	uint8		misc;
} _PACKED;


struct transfer_data {
	union {
		physical_entry* phys_vecs;
		iovec* vecs;
	};
	uint32 vec_count = 0;
	bool physical = false;
};


struct DeviceLun: public DevFsNode, public DevFsNodeHandle, public IOCallback {
public:
	status_t SendDiagnostic();
	status_t RequestSense(err_act *_action);
	status_t TestUnitReady(err_act* _action);
	status_t Inquiry();
	void ResetCapacity();
	status_t UpdateCapacity();
	status_t UpdateCapacity16();
	status_t Synchronize(bool force);

	status_t OperationInterrupt(uint8* operation, const transfer_data& data, size_t *dataLength, bool directionIn, err_act *_action);
	status_t OperationBulk(uint8 *operation, size_t operationLength, const transfer_data& data, size_t *dataLength, bool directionIn, err_act *_action);
	status_t Operation(uint8* operation, size_t opLength, const transfer_data& data, size_t *dataLength, bool directionIn, err_act *_action = NULL);
	status_t Operation(uint8* operation, size_t opLength, void *buffer, size_t *dataLength, bool directionIn, err_act *_action = NULL);

	status_t HandleMediaChange(MutexLocker& locker);

	status_t BlockRead(uint64 blockPosition, size_t blockCount, struct transfer_data data, size_t *length);
	status_t BlockWrite(uint64 blockPosition, size_t blockCount, struct transfer_data data, size_t *length);

	// DevFsNode
	Capabilities GetCapabilities() const final {return {.io = true, .control = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

	// DevFsNodeHandle
	status_t Close() final;
	status_t IO(io_request *request) final;
	status_t Control(uint32 op, void *buffer, size_t length) final;

	// IOCallback
	status_t DoIO(IOOperation* operation) final;

public:
	UsbDiskDriver* fDevice;
	char		fName[32];
	uint8		fLogicalUnitNumber;
	bool		fShouldSync;

	// device information through read capacity/inquiry
	bool		fMediaPresent;
	bool		fMediaChanged;
	uint64		fBlockCount;
	uint32		fBlockSize;
	uint32		fPhysicalBlockSize;
	uint8		fDeviceType;
	bool		fRemovable;
	bool		fWriteProtected;

	char		fVendorName[8];
	char		fProductName[16];
	char		fProductRevision[4];

	// new fields
	ObjectDeleter<DMAResource> fDmaResource;
	ObjectDeleter<IOScheduler> fIoScheduler;
};


class UsbDiskDriver: public DeviceDriver {
public:
	UsbDiskDriver(DeviceNode* node): fNode(node) {}
	virtual ~UsbDiskDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	void DeviceRemoved() final;

	void ResetRecovery(err_act* _action);
	status_t TransferData(bool directionIn, const transfer_data& data);
	status_t TransferData(bool directionIn, void* buffer, size_t dataLength);
	status_t ReceiveCswInterrupt(interrupt_status_wrapper* status);
	status_t ReceiveCswBulk(usb_massbulk_command_status_wrapper* status);

	status_t AcquireIoLock(MutexLocker& locker, RecursiveLocker& ioLocker);

private:
	status_t Init();

	uint8 GetMaxLun();
	status_t MassStorageReset();
	static void Callback(void *cookie, status_t status, void *data, size_t actualLength);
	static void CallbackInterrupt(void* cookie, int32 status, void* data, size_t length);

private:
	friend struct DeviceLun;

	DeviceNode* fNode;
	int32 fNumber {};

	UsbDevice* fDevice {};
	bool fRemoved {};
	recursive_lock fIoLock = RECURSIVE_LOCK_INITIALIZER("usb_disk i/o lock");
	mutex fLock = MUTEX_INITIALIZER("usb_disk device lock");

	// device state
	UsbPipe* fBulkIn {};
	UsbPipe* fBulkOut {};
	UsbPipe* fInterrupt {};
	UsbInterface* fInterface {};
	uint32 fCurrentTag {};
	uint8 fSyncSupport = SYNC_SUPPORT_RELOAD;
	bool fTurSupported = true;
	bool fIsAtapi = false;
	bool fIsUfi = false;

	// used to store callback information
	SemDeleter fNotify;
	status_t fStatus = -1;
	size_t fActualLength {};

	// used to store interrupt result
	unsigned char fInterruptBuffer[2] {};
	SemDeleter fInterruptLock;

	// logical units of this device
	uint8 fLunCount {};
	ArrayDeleter<DeviceLun> fLuns {};
};
