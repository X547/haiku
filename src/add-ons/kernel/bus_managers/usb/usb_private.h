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


#include <dm2/bus/USB.h>

#include "usbspec_private.h"
#include <lock.h>
#include <util/Vector.h>

// include vm.h before iovec_support.h for generic_memcpy, which is used by the bus drivers.
#include <vm/vm.h>
#include <util/iovec_support.h>


#define TRACE_OUTPUT(x, y, z...) \
	{ \
		dprintf("usb %s%s %" B_PRId32 ": ", y, (x)->TypeName(), (x)->USBID()); \
		dprintf(z); \
	}

#define TRACE_USB
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


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class Hub;
class Stack;
class Device;
class Transfer;
class BusManager;
class Pipe;
class ControlPipe;
class Object;
class PhysicalMemoryAllocator;


class Stack : public UsbStack {
public:
static	Stack &							Instance();

										Stack();
										~Stack();


		status_t						Init();

		bool							Lock() final;
		void							Unlock() final;

		usb_id							GetUSBID(UsbBusObject *object) final;
		void							PutUSBID(UsbBusObject *object) final;

		// This sets the object as busy; the caller must set it un-busy.
		UsbBusObject *					GetObject(usb_id id) final;

		// only for the kernel debugger
		UsbBusObject *					GetObjectNoLock(usb_id id) const final;

		void							AddBusManager(UsbBusManager *bus) final;
		int32							IndexOfBusManager(UsbBusManager *bus) final;
		UsbBusManager *					BusManagerAt(int32 index) const final;

		status_t						AllocateChunk(void **logicalAddress,
											phys_addr_t *physicalAddress,
											size_t size) final;
		status_t						FreeChunk(void *logicalAddress,
											phys_addr_t physicalAddress,
											size_t size) final;

		area_id							AllocateArea(void **logicalAddress,
											phys_addr_t *physicalAddress,
											size_t size, const char *name) final;

		usb_id							USBID() const final { return 0; }
		const char *					TypeName() const final { return "stack"; }

		void							Explore() final;


// new methods
		status_t						CreateDevice(UsbBusDevice*& outDevice,
											UsbBusObject* parent, int8 hubAddress,
											uint8 hubPort,
											usb_device_descriptor& desc,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub,
											void *controllerCookie = NULL) final;

		status_t						CreateHub(UsbBusHub*& outHub,
											UsbBusObject* parent, int8 hubAddress,
											uint8 hubPort,
											usb_device_descriptor& desc,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub,
											void* controllerCookie = NULL) final;

		status_t						CreateControlPipe(UsbBusControlPipe*& outPipe,
											UsbBusObject* parent,
											int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											UsbBusPipe::pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort) final;

private:
static	int32							ExploreThread(void *data);

static	Stack							sInstance;

		Vector<UsbBusManager *>			fBusManagers;
		thread_id						fExploreThread;
		sem_id							fExploreSem;

		mutex							fStackLock;
		mutex							fExploreLock;
		PhysicalMemoryAllocator *		fAllocator;

		uint32							fObjectIndex;
		uint32							fObjectMaxCount;
		UsbBusObject **					fObjectArray;
};


/*
 * This class manages a bus. It is created by the Stack object
 * after a host controller gives positive feedback on whether the hardware
 * is found.
 */
class BusManager : public UsbBusManager {
public:
										BusManager(Stack *stack, DeviceNode* node);
virtual									~BusManager();

		status_t						Init();

		bool							Lock() final;
		void							Unlock() final;

		int8							AllocateAddress() final;
		void							FreeAddress(int8 address) final;

		UsbBusObject *					RootObject() const final/*
											{ return fRootObject; }*/;

		UsbBusHub *						GetRootHub() const final /*{ return fRootHub; }*/;
		void							SetRootHub(UsbBusHub *hub) final /*{ fRootHub = static_cast<Hub*>(hub); }*/;

		DeviceNode *					Node() const final
											{ return fNode; }

private:
		ControlPipe *					_GetDefaultPipe(usb_speed);

		mutex							fLock;

		bool							fDeviceMap[128];
		int8							fDeviceIndex;

		Stack *							fStack;
		ControlPipe *					fDefaultPipes[USB_SPEED_MAX + 1];
		Hub *							fRootHub;
		Object *						fRootObject;

		usb_id							fStackIndex;

		DeviceNode *					fNode;

		UsbHostController *				fHostController;
};


class Object : public UsbBusObject {
public:
										Object(Stack *stack, BusManager *bus);
										Object(Object *parent);
virtual									~Object();

		UsbBusObject *						Parent() const final { return fParent; }

		UsbBusManager *					GetBusManager() const final
											{ return fBusManager; }
		UsbStack *							GetStack() const final { return fStack; }

		usb_id							USBID() const final { return fUSBID; }
		void							SetBusy(bool busy) final
											{ atomic_add(&fBusy, busy ? 1 : -1); }

		uint32							Type() const override { return USB_OBJECT_NONE; }
		const char *					TypeName() const override { return "object"; }

		// Convenience functions for standard requests
virtual	status_t						SetFeature(uint16 selector) override;
virtual	status_t						ClearFeature(uint16 selector) override;
virtual	status_t						GetStatus(uint16 *status) override;

protected:
		void							PutUSBID(bool waitForUnbusy = true);
		void							WaitForUnbusy();

private:
		Object *						fParent;
		BusManager *					fBusManager;
		Stack *							fStack;
		usb_id							fUSBID;
		int32							fBusy;
};


/*
 * The Pipe class is the communication management between the hardware and
 * the stack. It creates packets, manages these and performs callbacks.
 */
class Pipe : public UsbBusPipe, public Object {
public:
										Pipe(Object *parent);
virtual									~Pipe();

		void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort) override;
		void							InitSuperSpeed(uint8 maxBurst,
											uint16 bytesPerInterval) final;

		uint32							Type() const override { return USB_OBJECT_PIPE; }
		const char *					TypeName() const override { return "pipe"; }

		int8							DeviceAddress() const final
											{ return fDeviceAddress; }
		usb_speed						Speed() const final { return fSpeed; }
		pipeDirection					Direction() const final { return fDirection; }
		uint8							EndpointAddress() const final
											{ return fEndpointAddress; }
		size_t							MaxPacketSize() const final
											{ return fMaxPacketSize; }
		uint8							Interval() const final { return fInterval; }

		// SuperSpeed-only parameters
		uint8							MaxBurst() const final
											{ return fMaxBurst; }
		uint16							BytesPerInterval() const final
											{ return fBytesPerInterval; }

		// Hub port being the one-based logical port number on the hub
		void							SetHubInfo(int8 address, uint8 port) final;
		int8							HubAddress() const final
											{ return fHubAddress; }
		uint8							HubPort() const final { return fHubPort; }

		bool							DataToggle() const override
											{ return fDataToggle; }
		void							SetDataToggle(bool toggle) override
											{ fDataToggle = toggle; }

		status_t						SubmitTransfer(UsbBusTransfer *transfer) final;
		status_t						CancelQueuedTransfers(bool force) override;

		void							SetControllerCookie(void *cookie) final
											{ fControllerCookie = cookie; }
		void *							ControllerCookie() const final
											{ return fControllerCookie; }

		// Convenience functions for standard requests
		status_t						SetFeature(uint16 selector) final;
		status_t						ClearFeature(uint16 selector) final;
		status_t						GetStatus(uint16 *status) final;

protected:
		friend class					Device;

private:
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
};


class ControlPipe : public UsbBusControlPipe, public Pipe {
public:
										ControlPipe(Object *parent);
virtual									~ControlPipe();

		void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort) final;

		uint32							Type() const final { return USB_OBJECT_PIPE
											| USB_OBJECT_CONTROL_PIPE; }
		const char *					TypeName() const final
											{ return "control pipe"; }

										// The data toggle is not relevant
										// for control transfers, as they are
										// always enclosed by a setup and
										// status packet. The toggle always
										// starts at 1.
		bool							DataToggle() const final { return true; }
		void							SetDataToggle(bool toggle) final {}

		status_t						SendRequest(uint8 requestType,
											uint8 request, uint16 value,
											uint16 index, uint16 length,
											void *data, size_t dataLength,
											size_t *actualLength) final;
static	void							SendRequestCallback(void *cookie,
											status_t status, void *data,
											size_t actualLength);

		status_t						QueueRequest(uint8 requestType,
											uint8 request, uint16 value,
											uint16 index, uint16 length,
											void *data, size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);

		status_t						CancelQueuedTransfers(bool force) final;

private:
		mutex							fSendRequestLock;
		sem_id							fNotifySem;
		status_t						fTransferStatus;
		size_t							fActualLength;
};


class InterruptPipe : public Pipe {
public:
										InterruptPipe(Object *parent);

virtual	uint32							Type() const final { return USB_OBJECT_PIPE
											| USB_OBJECT_INTERRUPT_PIPE; }
virtual	const char *					TypeName() const final
											{ return "interrupt pipe"; }

		status_t						QueueInterrupt(void *data,
											size_t dataLength,
											usb_callback_func callback,
											void *callbackCookie);
};


class BulkPipe : public Pipe {
public:
										BulkPipe(Object *parent);

		void							InitCommon(int8 deviceAddress,
											uint8 endpointAddress,
											usb_speed speed,
											pipeDirection direction,
											size_t maxPacketSize,
											uint8 interval,
											int8 hubAddress, uint8 hubPort) final;

		uint32							Type() const final { return USB_OBJECT_PIPE
											| USB_OBJECT_BULK_PIPE; }
		const char *					TypeName() const final { return "bulk pipe"; }

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
										IsochronousPipe(Object *parent);

		uint32							Type() const final { return USB_OBJECT_PIPE
											| USB_OBJECT_ISO_PIPE; }
		const char *					TypeName() const final { return "iso pipe"; }

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
										Interface(Object *parent,
											uint8 interfaceIndex);

		uint32							Type() const final
											{ return USB_OBJECT_INTERFACE; }
		const char *					TypeName() const final { return "interface"; }

		// Convenience functions for standard requests
		status_t						SetFeature(uint16 selector) final;
		status_t						ClearFeature(uint16 selector) final;
		status_t						GetStatus(uint16 *status) final;

private:
		uint8							fInterfaceIndex;
};


class Device : public UsbBusDevice, public Object {
public:
										Device(Object *parent, int8 hubAddress,
											uint8 hubPort,
											usb_device_descriptor &desc,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub,
											void *controllerCookie = NULL);
virtual									~Device();

		status_t						InitCheck();

		status_t						Changed(change_item **changeList,
											bool added) override;

		uint32							Type() const override
											{ return USB_OBJECT_DEVICE; }
		const char *					TypeName() const override { return "device"; }

		UsbBusControlPipe *				DefaultPipe() const final/*
											{ return fDefaultPipe; }*/;

		status_t						GetDescriptor(uint8 descriptorType,
											uint8 index, uint16 languageID,
											void *data, size_t dataLength,
											size_t *actualLength) override;

		int8							DeviceAddress() const final
											{ return fDeviceAddress; }
		const usb_device_descriptor *	DeviceDescriptor() const final;
		usb_speed						Speed() const final { return fSpeed; }

		const usb_configuration_info *	Configuration() const final;
		const usb_configuration_info *	ConfigurationAt(uint8 index) const final;
		status_t						SetConfiguration(
											const usb_configuration_info *
												configuration) final;
		status_t						SetConfigurationAt(uint8 index) final;
		status_t						Unconfigure(bool atDeviceLevel) final;

		status_t						SetAltInterface(
											const usb_interface_info *
												interface) final;

		void							InitEndpoints(int32 interfaceIndex) final;
		void							ClearEndpoints(int32 interfaceIndex) final;

		status_t						BuildDeviceName(char *string,
											uint32 *index, size_t bufferSize,
											UsbBusDevice *device) override;

		DeviceNode *					RegisterNode(DeviceNode* parent = NULL) final;

		int8							HubAddress() const final
											{ return fHubAddress; }
		uint8							HubPort() const final { return fHubPort; }

		void							SetControllerCookie(void *cookie) final
											{ fControllerCookie = cookie; }
		void *							ControllerCookie() const final
											{ return fControllerCookie; }
		DeviceNode *					Node() const final
											{ return fNode; }
		void							SetNode(DeviceNode* node) final { fNode = node; }

		// Convenience functions for standard requests
		status_t						SetFeature(uint16 selector) final;
		status_t						ClearFeature(uint16 selector) final;
		status_t						GetStatus(uint16 *status) final;

protected:
		usb_device_descriptor			fDeviceDescriptor;
		bool							fInitOK;

private:
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
		DeviceNode *					fNode;
};


class Hub : public UsbBusHub, public Device {
public:
										Hub(Object *parent, int8 hubAddress,
											uint8 hubPort,
											usb_device_descriptor &desc,
											int8 deviceAddress,
											usb_speed speed, bool isRootHub,
											void *controllerCookie = NULL);
virtual									~Hub();

virtual	status_t						Changed(change_item **changeList,
											bool added);

virtual	uint32							Type() const final { return USB_OBJECT_DEVICE
											| USB_OBJECT_HUB; }
virtual	const char *					TypeName() const final { return "hub"; }

virtual	status_t						GetDescriptor(uint8 descriptorType,
											uint8 index, uint16 languageID,
											void *data, size_t dataLength,
											size_t *actualLength) final;

		Device *						ChildAt(uint8 index) const final
											{ return fChildren[index]; }

		status_t						UpdatePortStatus(uint8 index) final;
		status_t						ResetPort(uint8 index) final;
		status_t						DisablePort(uint8 index) final;

		void							Explore(change_item **changeList) final;
static	void							InterruptCallback(void *cookie,
											status_t status, void *data,
											size_t actualLength);

virtual	status_t						BuildDeviceName(char *string,
											uint32 *index, size_t bufferSize,
											UsbBusDevice *device) final;

private:
		status_t						_DebouncePort(uint8 index);

		InterruptPipe *					fInterruptPipe;
		usb_hub_descriptor				fHubDescriptor;

		usb_port_status					fInterruptStatus[USB_MAX_PORT_COUNT];
		usb_port_status					fPortStatus[USB_MAX_PORT_COUNT];
		Device *						fChildren[USB_MAX_PORT_COUNT];
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
class Transfer: public UsbBusTransfer {
public:
									Transfer(Pipe *pipe);
									~Transfer();

		UsbBusPipe *				TransferPipe() const final { return fPipe; }

		void						SetRequestData(usb_request_data *data) final;
		usb_request_data *			RequestData() const final { return fRequestData; }

		void						SetIsochronousData(
										usb_isochronous_data *data) final;
		usb_isochronous_data *		IsochronousData() const final
										{ return fIsochronousData; }

		void						SetData(uint8 *buffer, size_t length) final;
		uint8 *						Data() const final
										{ return fPhysical ? NULL : (uint8 *)fData.base; }
		size_t						DataLength() const final { return fData.length; }

		bool						IsPhysical() const final { return fPhysical; }

		void						SetVector(iovec *vector, size_t vectorCount) final;
		void						SetVector(physical_entry *vector, size_t vectorCount) final;
		generic_io_vec *			Vector() final { return fVector; }
		size_t						VectorCount() const final { return fVectorCount; }

		uint16						Bandwidth() const final { return fBandwidth; }

		bool						IsFragmented() const final { return fFragmented; }
		void						AdvanceByFragment(size_t actualLength) final;
		size_t						FragmentLength() const final;

		status_t					InitKernelAccess() final;
		status_t					PrepareKernelAccess() final;

		void						SetCallback(usb_callback_func callback,
										void *cookie) final;
		usb_callback_func			Callback() const final
										{ return fCallback; }
		void *						CallbackCookie() const final
										{ return fCallbackCookie; }

		void						Finished(uint32 status,
										size_t actualLength) final;

		usb_id						USBID() const final { return 0; }
		const char *				TypeName() const final { return "transfer"; }

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
};


#endif // _USB_PRIVATE_H
