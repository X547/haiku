/*
 * Copyright 2003-2006, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Niels S. Reedijk
 */
#ifndef _USB_PRIVATE_H
#define _USB_PRIVATE_H


#include <dm2/device_manager.h>
#include <dm2/bus/USB.h>

#include "usbspec_private.h"
#include <lock.h>
#include <util/Vector.h>

// include vm.h before iovec_support.h for generic_memcpy, which is used by the bus drivers.
#include <vm/vm.h>
#include <util/iovec_support.h>
#include <DPC.h>

#include "Dm2Interfaces.h"
#include "Dm2BusInterfaces.h"


#define TRACE_OUTPUT(x, y, z...) \
	{ \
		dprintf("usb: "); \
		(x)->DumpPath(); \
		dprintf(": "); \
		dprintf(z); \
	}

//#define TRACE_USB
#ifdef TRACE_USB
#define TRACE(x...)					TRACE_OUTPUT(this, "", x)
#define TRACE_STATIC(x, y...)		TRACE_OUTPUT(x, "", y)
#define TRACE_MODULE(x...)			dprintf("usb " USB_MODULE_NAME ": " x)
#else
#define TRACE(x...)					/* nothing */
#define TRACE_STATIC(x, y...)		/* nothing */
#define TRACE_MODULE(x...)			/* nothing */
#endif

#define TRACE_ALWAYS(x...)			TRACE_OUTPUT(this, "", x)
#define TRACE_ERROR(x...)			TRACE_OUTPUT(this, "error ", x)
#define TRACE_MODULE_ALWAYS(x...)	dprintf("usb " USB_MODULE_NAME ": " x)
#define TRACE_MODULE_ERROR(x...)	dprintf("usb " USB_MODULE_NAME ": " x)

extern device_manager_info *gDeviceManager;


class Hub;
class Stack;
class Device;
class Transfer;
class BusManager;
class Pipe;
class ControlPipe;
class Object;
class PhysicalMemoryAllocator;


struct usb_driver_cookie {
	usb_id device;
	void *cookie;
	usb_driver_cookie *link;
};


struct change_item {
	bool added;
	Device *device;
	change_item *link;
};


#if 0
typedef enum {
	USB_SPEED_LOWSPEED = 0,
	USB_SPEED_FULLSPEED,
	USB_SPEED_HIGHSPEED,
	USB_SPEED_SUPERSPEED,
	USB_SPEED_MAX = USB_SPEED_SUPERSPEED
} usb_speed;


typedef enum {
	USB_CHANGE_CREATED = 0,
	USB_CHANGE_DESTROYED,
	USB_CHANGE_PIPE_POLICY_CHANGED
} usb_change;
#endif


#define USB_OBJECT_NONE					0x00000000
#define USB_OBJECT_PIPE					0x00000001
#define USB_OBJECT_CONTROL_PIPE			0x00000002
#define USB_OBJECT_INTERRUPT_PIPE		0x00000004
#define USB_OBJECT_BULK_PIPE			0x00000008
#define USB_OBJECT_ISO_PIPE				0x00000010
#define USB_OBJECT_INTERFACE			0x00000020
#define USB_OBJECT_DEVICE				0x00000040
#define USB_OBJECT_HUB					0x00000080


class Stack {
public:
static	Stack &							Instance();

										Stack();
										~Stack();

		UsbStack *						GetStackIface() {return &fStackIface;}

		status_t						InitCheck();

		bool							Lock();
		void							Unlock();

		usb_id							GetUSBID(Object *object);
		void							PutUSBID(Object *object);

		// This sets the object as busy; the caller must set it un-busy.
		Object *						GetObject(usb_id id);

		// only for the kernel debugger
		Object *						GetObjectNoLock(usb_id id) const;

		void							AddBusManager(BusManager *bus);
		int32							IndexOfBusManager(BusManager *bus);
		BusManager *					BusManagerAt(int32 index) const;

		status_t						AllocateChunk(void **logicalAddress,
											phys_addr_t *physicalAddress,
											size_t size);
		status_t						FreeChunk(void *logicalAddress,
											phys_addr_t physicalAddress,
											size_t size);

		area_id							AllocateArea(void **logicalAddress,
											phys_addr_t *physicalAddress,
											size_t size, const char *name);

		void							DumpPath() const {dprintf("stack");}

		usb_id							USBID() const { return 0; }
		const char *					TypeName() const { return "stack"; }

private:
static	Stack							sInstance;

		Vector<BusManager *>			fBusManagers;

		mutex							fStackLock;
		PhysicalMemoryAllocator *		fAllocator;

		uint32							fObjectIndex;
		uint32							fObjectMaxCount;
		Object **						fObjectArray;

		UsbStackImpl					fStackIface;
};


/*
 * This class manages a bus. It is created by the Stack object
 * after a host controller gives positive feedback on whether the hardware
 * is found.
 */
class BusManager {
public:
										BusManager(UsbHostController* hostCtrl, DeviceNode* node);
virtual									~BusManager();
		UsbBusManager *					GetBusManagerIface() {return &fBusManagerIface;}

virtual	status_t						InitCheck();

		bool							Lock();
		void							Unlock();

		int8							AllocateAddress();
		void							FreeAddress(int8 address);

inline	status_t						InitDevice(Device *device, const usb_device_descriptor &deviceDescriptor);
inline	status_t						InitHub(Device *hub, const usb_hub_descriptor &hubDescriptor);
		Device *						AllocateDevice(Device *parent,
											int8 hubAddress, uint8 hubPort,
											usb_speed speed);
		void							FreeDevice(Device *device);

		status_t						Start();
		status_t						Stop();

inline	status_t						StartDebugTransfer(Transfer *transfer);
inline	status_t						CheckDebugTransfer(Transfer *transfer);
inline	void							CancelDebugTransfer(Transfer *transfer);

inline	status_t						SubmitTransfer(Transfer *transfer);
inline	status_t						CancelQueuedTransfers(Pipe *pipe,
											bool force);

inline	status_t						NotifyPipeChange(Pipe *pipe,
											usb_change change);

		Device *						GetRootHub() const { return fRootHub; }
		void							SetRootHub(Device *hub) { fRootHub = hub; }

		const char *					TypeName() const {return fHostController->TypeName();}

		DeviceNode *					Node() const
											{ return fNode; }

		void							DumpPath() const {dprintf("bus(%" B_PRIu32 ")", fStackIndex);}

protected:
		usb_id							USBID() const { return fStackIndex; }

protected:
		bool							fInitOK;

private:
		UsbHostController *				fHostController;

		mutex							fLock;

		bool							fDeviceMap[128];
		int8							fDeviceIndex;

		Device *						fRootHub;

		usb_id							fStackIndex;

		DeviceNode*						fNode;

		UsbBusManagerImpl				fBusManagerIface;
};


class Object {
public:
										Object(BusManager *bus);
virtual									~Object();

		UsbObject *						GetObjectIface() {return &fObjectIface;}

		BusManager *					GetBusManager() const
											{ return fBusManager; }

		usb_id							USBID() const { return fUSBID; }
		void							SetBusy(bool busy)
											{ atomic_add(&fBusy, busy ? 1 : -1); }

virtual	uint32							Type() const { return USB_OBJECT_NONE; }
virtual	const char *					TypeName() const { return "object"; }

virtual	void							DumpPath() const;

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

protected:
		void							PutUSBID(bool waitForUnbusy = true);
		void							WaitForUnbusy();

private:
		BusManager *					fBusManager;
		usb_id							fUSBID;
		int32							fBusy;

		UsbObjectImpl					fObjectIface;
};


/*
 * The Pipe class is the communication management between the hardware and
 * the stack. It creates packets, manages these and performs callbacks.
 */
class Pipe : public Object {
public:
		enum pipeDirection { In, Out, Default };

										Pipe(Device *parent);
virtual									~Pipe();

		UsbPipe *						GetPipeIface() {return &fPipeIface;}
		UsbBusPipe *					GetBusPipeIface() {return &fBusPipeIface;}

		Device *						Parent() const {return fParent;}

virtual	void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort);
virtual void							InitSuperSpeed(uint8 maxBurst,
											uint16 bytesPerInterval);

virtual	uint32							Type() const { return USB_OBJECT_PIPE; }
virtual	const char *					TypeName() const { return "pipe"; }

		int8							DeviceAddress() const
											{ return fDeviceAddress; }
		usb_speed						Speed() const { return fSpeed; }
		pipeDirection					Direction() const { return fDirection; }
		uint8							EndpointAddress() const
											{ return fEndpointAddress; }
		size_t							MaxPacketSize() const
											{ return fMaxPacketSize; }
		uint8							Interval() const { return fInterval; }

		// SuperSpeed-only parameters
		uint8							MaxBurst() const
											{ return fMaxBurst; }
		uint16							BytesPerInterval() const
											{ return fBytesPerInterval; }

		// Hub port being the one-based logical port number on the hub
		void							SetHubInfo(int8 address, uint8 port);
		int8							HubAddress() const
											{ return fHubAddress; }
		uint8							HubPort() const { return fHubPort; }

virtual	bool							DataToggle() const
											{ return fDataToggle; }
virtual	void							SetDataToggle(bool toggle)
											{ fDataToggle = toggle; }

		status_t						SubmitTransfer(Transfer *transfer);
virtual	status_t						CancelQueuedTransfers(bool force);

		void							SetControllerCookie(void *cookie)
											{ fControllerCookie = cookie; }
		void *							ControllerCookie() const
											{ return fControllerCookie; }

virtual	void							DumpPath() const;

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

protected:
		friend class					Device;

private:
		Device *						fParent;
		int8							fDeviceAddress;
		uint8							fEndpointAddress;
		pipeDirection					fDirection;
		usb_speed						fSpeed;
		size_t							fMaxPacketSize;
		uint8							fInterval;
		uint8							fMaxBurst;
		uint16							fBytesPerInterval;
		int8							fHubAddress;
		uint8							fHubPort;
		bool							fDataToggle;
		void *							fControllerCookie;

		UsbPipeImpl						fPipeIface;
		UsbBusPipeImpl					fBusPipeIface;
};


class ControlPipe : public Pipe {
public:
										ControlPipe(Device *parent);
virtual									~ControlPipe();

virtual	void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort);

virtual	uint32							Type() const { return USB_OBJECT_PIPE
											| USB_OBJECT_CONTROL_PIPE; }
virtual	const char *					TypeName() const
											{ return "control pipe"; }

										// The data toggle is not relevant
										// for control transfers, as they are
										// always enclosed by a setup and
										// status packet. The toggle always
										// starts at 1.
virtual	bool							DataToggle() const { return true; }
virtual	void							SetDataToggle(bool toggle) {}

		status_t						SendRequest(uint8 requestType,
											uint8 request, uint16 value,
											uint16 index, uint16 length,
											void *data, size_t dataLength,
											size_t *actualLength);
static	void							SendRequestCallback(void *cookie,
											status_t status, void *data,
											size_t actualLength);

		status_t						QueueRequest(uint8 requestType,
											uint8 request, uint16 value,
											uint16 index, uint16 length,
											void *data, size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);

virtual	status_t						CancelQueuedTransfers(bool force);

private:
		mutex							fSendRequestLock;
		sem_id							fNotifySem;
		status_t						fTransferStatus;
		size_t							fActualLength;
};


class InterruptPipe : public Pipe {
public:
										InterruptPipe(Device *parent);

virtual	uint32							Type() const { return USB_OBJECT_PIPE
											| USB_OBJECT_INTERRUPT_PIPE; }
virtual	const char *					TypeName() const
											{ return "interrupt pipe"; }

		status_t						QueueInterrupt(void *data,
											size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);
};


class BulkPipe : public Pipe {
public:
										BulkPipe(Device *parent);

virtual	void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort);

virtual	uint32							Type() const { return USB_OBJECT_PIPE
											| USB_OBJECT_BULK_PIPE; }
virtual	const char *					TypeName() const { return "bulk pipe"; }

		status_t						QueueBulk(void *data,
											size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);
		status_t						QueueBulkV(iovec *vector, size_t vectorCount,
											usb_callback_func callback, void *callbackCookie);
		status_t						QueueBulkV(physical_entry *vector, size_t vectorCount,
											usb_callback_func callback, void *callbackCookie);};


class IsochronousPipe : public Pipe {
public:
										IsochronousPipe(Device *parent);

virtual	uint32							Type() const { return USB_OBJECT_PIPE
											| USB_OBJECT_ISO_PIPE; }
virtual	const char *					TypeName() const { return "iso pipe"; }

		status_t						QueueIsochronous(void *data,
											size_t dataLength,
											usb_iso_packet_descriptor *
												packetDescriptor,
											uint32 packetCount,
											uint32 *startingFrameNumber,
											uint32 flags,
											usb_callback_func callback,
											void *callbackCookie);

		status_t						SetPipePolicy(uint8 maxQueuedPackets,
											uint16 maxBufferDurationMS,
											uint16 sampleSize);
		status_t						GetPipePolicy(uint8 *maxQueuedPackets,
											uint16 *maxBufferDurationMS,
											uint16 *sampleSize);

private:
		uint8							fMaxQueuedPackets;
		uint16							fMaxBufferDuration;
		uint16							fSampleSize;
};


class Interface : public Object {
public:
										Interface(Device *parent,
											uint8 interfaceIndex);

		UsbInterface *					GetInterfaceIface() {return &fInterfaceIface;}

		Device *						Parent() const {return fParent;}

virtual	uint32							Type() const
											{ return USB_OBJECT_INTERFACE; }
virtual	const char *					TypeName() const { return "interface"; }

		uint8							InterfaceIndex() const {return fInterfaceIndex;}

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

private:
		Device *						fParent;
		uint8							fInterfaceIndex;

		UsbInterfaceImpl				fInterfaceIface;
};


class Device : public Object {
public:
										Device(BusManager *busManager, Device *parent, int8 hubAddress,
											uint8 hubPort,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub,
											void *controllerCookie = NULL);
virtual									~Device();

		Device *						Parent() const {return fParent;}

		UsbDevice *						GetDeviceIface() {return &fDeviceIface;}
		UsbBusDevice *					GetBusDeviceIface() {return &fBusDeviceIface;}

		status_t						InitCheck();

virtual	status_t						Changed(change_item **changeList,
											bool added);

virtual	uint32							Type() const
											{ return USB_OBJECT_DEVICE; }
virtual	const char *					TypeName() const { return "device"; }

		ControlPipe *					DefaultPipe() const
											{ return fDefaultPipe; }

virtual	status_t						GetDescriptor(uint8 descriptorType,
											uint8 index, uint16 languageID,
											void *data, size_t dataLength,
											size_t *actualLength);

		int8							DeviceAddress() const
											{ return fDeviceAddress; }
		const usb_device_descriptor *	DeviceDescriptor() const;
		usb_speed						Speed() const { return fSpeed; }

		const usb_configuration_info *	Configuration() const;
		const usb_configuration_info *	ConfigurationAt(uint8 index) const;
		status_t						SetConfiguration(
											const usb_configuration_info *
												configuration);
		status_t						SetConfigurationAt(uint8 index);
		status_t						Unconfigure(bool atDeviceLevel);

		status_t						SetAltInterface(
											const usb_interface_info *
												interface);

		void							InitEndpoints(int32 interfaceIndex);
		void							ClearEndpoints(int32 interfaceIndex);

		DeviceNode *					RegisterNode(DeviceNode* parent = NULL);

		int8							HubAddress() const
											{ return fHubAddress; }
		uint8							HubPort() const { return fHubPort; }

		void							SetControllerCookie(void *cookie)
											{ fControllerCookie = cookie; }
		void *							ControllerCookie() const
											{ return fControllerCookie; }
		DeviceNode *					Node() const
											{ return fNode; }
		void							SetNode(DeviceNode* node) { fNode = node; }

virtual	void							DumpPath() const;

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector);
virtual	status_t						ClearFeature(uint16 selector);
virtual	status_t						GetStatus(uint16 *status);

protected:
		usb_device_descriptor			fDeviceDescriptor;
		bool							fInitOK;

private:
		Device *						fParent;
		bool							fAvailable;
		bool							fIsRootHub;
		usb_configuration_info *		fConfigurations;
		usb_configuration_info *		fCurrentConfiguration;
		usb_speed						fSpeed;
		int8							fDeviceAddress;
		int8							fHubAddress;
		uint8							fHubPort;
		ControlPipe *					fDefaultPipe;
		void *							fControllerCookie;
		DeviceNode*						fNode;

		UsbDeviceImpl					fDeviceIface;
		UsbBusDeviceImpl				fBusDeviceIface;
};


/*
 * A Transfer is allocated on the heap and passed to the Host Controller in
 * SubmitTransfer(). It is generated for all queued transfers. If queuing
 * succeds (SubmitTransfer() returns with >= B_OK) the Host Controller takes
 * ownership of the Transfer and will delete it as soon as it has called the
 * set callback function. If SubmitTransfer() failes, the calling function is
 * responsible for deleting the Transfer.
 * Also, the transfer takes ownership of the usb_request_data passed to it in
 * SetRequestData(), but does not take ownership of the data buffer set by
 * SetData().
 */
class Transfer {
public:
									Transfer(Pipe *pipe);
									~Transfer();

		UsbBusTransfer *			GetBusTransferIface() {return &fBusTransferIface;}

		Pipe *						TransferPipe() const { return fPipe; }

		void						SetRequestData(usb_request_data *data);
		usb_request_data *			RequestData() const { return fRequestData; }

		void						SetIsochronousData(
										usb_isochronous_data *data);
		usb_isochronous_data *		IsochronousData() const
										{ return fIsochronousData; }

		void						SetData(uint8 *buffer, size_t length);
		uint8 *						Data() const
										{ return fPhysical ? NULL : (uint8 *)fData.base; }
		size_t						DataLength() const { return fData.length; }

		bool						IsPhysical() const { return fPhysical; }

		void						SetVector(iovec *vector, size_t vectorCount);
		void						SetVector(physical_entry *vector, size_t vectorCount);
		generic_io_vec *			Vector() { return fVector; }
		size_t						VectorCount() const { return fVectorCount; }

		uint16						Bandwidth() const { return fBandwidth; }

		bool						IsFragmented() const { return fFragmented; }
		void						AdvanceByFragment(size_t actualLength);
		size_t						FragmentLength() const;

		status_t					InitKernelAccess();
		status_t					PrepareKernelAccess();

		void						SetCallback(usb_callback_func callback,
										void *cookie);
		usb_callback_func			Callback() const
										{ return fCallback; }
		void *						CallbackCookie() const
										{ return fCallbackCookie; }

		void						Finished(uint32 status,
										size_t actualLength);

		void						DumpPath() const {dprintf("transfer");}

		usb_id						USBID() const { return 0; }
		const char *				TypeName() const { return "transfer"; }

private:
		void						_CheckFragmented();
		status_t					_CalculateBandwidth();

		// Data that is related to the transfer
		Pipe *						fPipe;
		generic_io_vec				fData;
		generic_io_vec *			fVector;
		size_t						fVectorCount;
		void *						fBaseAddress;
		bool						fPhysical;
		bool						fFragmented;
		size_t						fActualLength;
		area_id						fUserArea;
		area_id						fClonedArea;

		usb_callback_func			fCallback;
		void *						fCallbackCookie;

		// For control transfers
		usb_request_data *			fRequestData;

		// For isochronous transfers
		usb_isochronous_data *		fIsochronousData;

		// For bandwidth management.
		// It contains the bandwidth necessary in microseconds
		// for either isochronous, interrupt or control transfers.
		// Not used for bulk transactions.
		uint16						fBandwidth;

		UsbBusTransferImpl			fBusTransferIface;
};


status_t
BusManager::InitDevice(Device *device, const usb_device_descriptor &deviceDescriptor)
{
	return fHostController->InitDevice(device->GetBusDeviceIface(), deviceDescriptor);
}


status_t
BusManager::InitHub(Device *hub, const usb_hub_descriptor &hubDescriptor)
{
	return fHostController->InitHub(hub->GetBusDeviceIface(), hubDescriptor);
}


status_t
BusManager::StartDebugTransfer(Transfer *transfer)
{
	return fHostController->StartDebugTransfer(transfer->GetBusTransferIface());
}


status_t
BusManager::CheckDebugTransfer(Transfer *transfer)
{
	return fHostController->CheckDebugTransfer(transfer->GetBusTransferIface());
}


void
BusManager::CancelDebugTransfer(Transfer *transfer)
{
	fHostController->CancelDebugTransfer(transfer->GetBusTransferIface());
}


status_t
BusManager::SubmitTransfer(Transfer *transfer)
{
	return fHostController->SubmitTransfer(transfer->GetBusTransferIface());
}


status_t
BusManager::CancelQueuedTransfers(Pipe *pipe, bool force)
{
	return fHostController->CancelQueuedTransfers(pipe->GetBusPipeIface(), force);
}


status_t
BusManager::NotifyPipeChange(Pipe *pipe, usb_change change)
{
	return fHostController->NotifyPipeChange(pipe->GetBusPipeIface(), change);
}


#endif // _USB_PRIVATE_H
