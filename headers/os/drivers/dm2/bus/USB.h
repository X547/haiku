#pragma once

#include <dm2/device_manager.h>

#include <KernelExport.h>
#include <iovec.h>

#include <USB_spec.h>
#include <USB_isochronous.h>


#define USB_DEVICE_ID_ITEM	"usb/id"
#define USB_DEVICE_CLASS	"usb/class"
#define USB_DEVICE_SUBCLASS	"usb/subclass"
#define USB_DEVICE_PROTOCOL	"usb/protocol"


class UsbDevice;
class UsbInterface;
class UsbPipe;


typedef struct usb_endpoint_info usb_endpoint_info;
typedef struct usb_interface_info usb_interface_info;
typedef struct usb_interface_list usb_interface_list;
typedef struct usb_configuration_info usb_configuration_info;

struct usb_endpoint_info {
	usb_endpoint_descriptor		*descr;			/* descriptor and handle */
	UsbPipe						*handle;		/* of this endpoint/pipe */
};

struct usb_interface_info {
	usb_interface_descriptor	*descr;			/* descriptor and handle */
	UsbInterface				*handle;		/* of this interface */

	size_t						endpoint_count;	/* count and list of endpoints */
	usb_endpoint_info			*endpoint;		/* in this interface */

	size_t						generic_count;	/* unparsed descriptors in */
	usb_descriptor				**generic;		/* this interface */
};

struct usb_interface_list {
	size_t						alt_count;		/* count and list of alternate */
	usb_interface_info			*alt;			/* interfaces available */

	usb_interface_info			*active;		/* currently active alternate */
};

struct usb_configuration_info {
	usb_configuration_descriptor *descr;		/* descriptor of this config */

	size_t						interface_count;/* interfaces in this config */
	usb_interface_list			*interface;
};

// Flags for queue_isochronous
#define	USB_ISO_ASAP	0x01

typedef void (*usb_callback_func)(void *cookie, status_t status, void *data, size_t actualLength);


class UsbObject {
public:
	/*
	 *	Standard device requests - convenience functions
	 *	The provided handle may be a usb_device, usb_pipe or usb_interface
	 */
	virtual status_t SetFeature(uint16 selector) = 0;
	virtual status_t ClearFeature(uint16 selector) = 0;
	virtual status_t GetStatus(uint16 *status) = 0;

protected:
	~UsbObject() = default;
};


class UsbDevice {
public:
	static inline const char ifaceName[] = "bus_managers/usb/device";

	virtual UsbObject 	*GetObject() = 0;

	/* Get the device descriptor of a device. */
	virtual const usb_device_descriptor
						*GetDeviceDescriptor() = 0;

	/* Get the nth supported configuration of a device*/
	virtual const usb_configuration_info
						*GetNthConfiguration(uint32 index) = 0;

	/* Get the current configuration */
	virtual const usb_configuration_info
						*GetConfiguration() = 0;

	/* Set the current configuration */
	virtual status_t	SetConfiguration(const usb_configuration_info *configuration) = 0;

	virtual status_t	SetAltInterface(const usb_interface_info *interface) = 0;

	virtual status_t	GetDescriptor(
							uint8 descriptorType, uint8 index,
							uint16 languageID, void *data,
							size_t dataLength,
							size_t *actualLength) = 0;

	/* Generic device request function - synchronous */
	virtual status_t	SendRequest(
							uint8 requestType, uint8 request,
							uint16 value, uint16 index,
							uint16 length, void *data,
							size_t *actualLength) = 0;

	virtual status_t	QueueRequest(
							uint8 requestType, uint8 request,
							uint16 value, uint16 index,
							uint16 length, void *data,
							usb_callback_func callback,
							void *callbackCookie) = 0;

	/* Cancel all pending async requests in a device control pipe */
	virtual status_t	CancelQueuedRequests() = 0;

protected:
	~UsbDevice() = default;
};


class UsbHub {
public:
	virtual UsbDevice 	*GetDevice() = 0;

	/*
	 * Hub interaction - These commands are only valid when used with a hub
	 * device handle. Use reset_port to trigger a reset of the port with index
	 * portIndex. This will cause a disconnect event for the attached device.
	 * With disable_port you can specify that the port at portIndex shall be
	 * disabled. This will also cause a disconnect event for the attached
	 * device. Use reset_port to reenable a previously disabled port.
	 */
	virtual status_t	ResetPort(uint8 portIndex) = 0;
	virtual status_t	DisablePort(uint8 portIndex) = 0;

protected:
	~UsbHub() = default;
};


class UsbInterface {
public:
	virtual UsbObject 	*GetObject() = 0;

protected:
	~UsbInterface() = default;
};


class UsbPipe {
public:
	virtual UsbObject 	*GetObject() = 0;

	/*
	 *	Asynchronous request queueing. These functions return immediately
	 *	and the return code only tells whether the _queuing_ of the request
	 *	was successful or not. It doesn't indicate transfer success or error.
	 *
	 *	When the transfer is finished, the provided callback function is
	 *	called with the transfer status, a pointer to the data buffer and
	 *	the actually transfered length as well as with the callbackCookie
	 *	that was used when queuing the request.
	 */
	virtual status_t	QueueInterrupt(
							void *data, size_t dataLength,
							usb_callback_func callback,
							void *callbackCookie) = 0;

	virtual status_t	QueueBulk(
							void *data, size_t dataLength,
							usb_callback_func callback,
							void *callbackCookie) = 0;

	virtual status_t	QueueBulkV(
							iovec *vector, size_t vectorCount,
							usb_callback_func callback,
							void *callbackCookie) = 0;

	virtual status_t	QueueBulkVPhysical(
							physical_entry *vectors, size_t vectorCount,
							usb_callback_func callback,
							void *callbackCookie) = 0;

	virtual status_t	QueueIsochronous(
							void *data, size_t dataLength,
							usb_iso_packet_descriptor *packetDesc,
							uint32 packetCount,
							uint32 *startingFrameNumber,
							uint32 flags,
							usb_callback_func callback,
							void *callbackCookie) = 0;


	virtual status_t	SetPipePolicy(
							uint8 maxNumQueuedPackets,
							uint16 maxBufferDurationMS,
							uint16 sampleSize) = 0;

	/* Cancel all pending async requests in a pipe */
	virtual status_t	CancelQueuedTransfers() = 0;

protected:
	~UsbPipe() = default;
};


// #pragma mark - Host controller interface

typedef uint32 usb_id;

// !!!
struct usb_isochronous_data;
struct usb_request_data;
struct generic_io_vec;

class UsbStack;
class UsbBusDevice;
class UsbBusPipe;
class UsbBusControlPipe;
class UsbBusManager;
class UsbBusTransfer;


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


#define USB_OBJECT_CONTROL_PIPE			0x00000002
#define USB_OBJECT_INTERRUPT_PIPE		0x00000004
#define USB_OBJECT_BULK_PIPE			0x00000008
#define USB_OBJECT_ISO_PIPE				0x00000010


class UsbBusDevice {
public:
	virtual void			Free() = 0;

	// NULL for root hub
	virtual UsbBusDevice *	Parent() = 0;

	virtual	int8			DeviceAddress() const = 0;
	virtual	usb_speed		Speed() const  = 0;

	virtual	DeviceNode *	RegisterNode(DeviceNode* parent = NULL) = 0;

	virtual	int8			HubAddress() const = 0;
	virtual	uint8			HubPort() const = 0;

	virtual	void			SetControllerCookie(void *cookie) = 0;
	virtual	void *			ControllerCookie() const = 0;
	virtual	DeviceNode *	Node() const = 0;
	virtual	void			SetNode(DeviceNode* node) = 0;

protected:
	~UsbBusDevice() = default;
};


class UsbBusPipe {
public:
	enum pipeDirection { In, Out, Default };

	virtual void Free() = 0;

	virtual UsbBusDevice 	*GetDevice() = 0;
	virtual	uint32			Type() const = 0;

virtual		int8			DeviceAddress() const = 0;
virtual		usb_speed		Speed() const = 0;
virtual		pipeDirection	Direction() const = 0;
virtual		uint8			EndpointAddress() const = 0;
virtual		size_t			MaxPacketSize() const = 0;
virtual		uint8			Interval() const = 0;

		// SuperSpeed-only parameters
virtual		uint8			MaxBurst() const = 0;
virtual		uint16			BytesPerInterval() const = 0;

		// Hub port being the one-based logical port number on the hub
virtual		void			SetHubInfo(int8 address, uint8 port) = 0;
virtual		int8			HubAddress() const = 0;
virtual		uint8			HubPort() const = 0;

virtual		bool			DataToggle() const = 0;
virtual		void			SetDataToggle(bool toggle) = 0;

virtual		status_t		SubmitTransfer(UsbBusTransfer *transfer) = 0;
virtual		status_t		CancelQueuedTransfers(bool force) = 0;

virtual		void			SetControllerCookie(void *cookie) = 0;
virtual		void *			ControllerCookie() const = 0;

virtual status_t			SendRequest(uint8 requestType,
								uint8 request, uint16 value,
								uint16 index, uint16 length,
								void *data, size_t dataLength,
								size_t *actualLength) = 0;
protected:
	~UsbBusPipe() = default;
};


class UsbBusTransfer {
public:
virtual		void						Free() = 0;

virtual		UsbBusPipe *				TransferPipe() const = 0;

virtual		usb_request_data *			RequestData() const = 0;

virtual		usb_isochronous_data *		IsochronousData() const = 0;

virtual		uint8 *						Data() const = 0;
virtual		size_t						DataLength() const = 0;

virtual		bool						IsPhysical() const = 0;

virtual		generic_io_vec *			Vector() = 0;
virtual		size_t						VectorCount() const = 0;

virtual		uint16						Bandwidth() const = 0;

virtual		bool						IsFragmented() const = 0;
virtual		void						AdvanceByFragment(size_t actualLength) = 0;
virtual		size_t						FragmentLength() const = 0;

virtual		status_t					InitKernelAccess() = 0;
virtual		status_t					PrepareKernelAccess() = 0;

virtual		void						SetCallback(usb_callback_func callback, void *cookie) = 0;
virtual		usb_callback_func			Callback() const = 0;
virtual		void *						CallbackCookie() const = 0;

virtual		void						Finished(uint32 status, size_t actualLength) = 0;

protected:
	~UsbBusTransfer() = default;
};


class UsbBusManager {
public:
	virtual void Free() = 0;

	virtual bool Lock() = 0;
	virtual void Unlock() = 0;

	virtual int32 ID() = 0;

	virtual	int8 AllocateAddress() = 0;
	virtual	void FreeAddress(int8 address) = 0;

	virtual UsbBusDevice* GetRootHub() const = 0;
	virtual void SetRootHub(UsbBusDevice* hub) = 0;
	virtual	DeviceNode* Node() const = 0;


	// new methods
	virtual status_t	CreateDevice(UsbBusDevice*& outDevice, UsbBusDevice* parent, int8 hubAddress,
							uint8 hubPort,
							usb_device_descriptor& desc,
							int8 deviceAddress,
							usb_speed speed, bool isRootHub,
							void *controllerCookie = NULL) = 0;

	virtual status_t	CreateHub(UsbBusDevice*& outDevice, UsbBusDevice* parent, int8 hubAddress,
							uint8 hubPort,
							usb_device_descriptor& desc,
							int8 deviceAddress,
							usb_speed speed, bool isRootHub,
							void* controllerCookie = NULL) = 0;

	virtual status_t	CreateControlPipe(UsbBusPipe*& outPipe, UsbBusDevice* parent,
							int8 deviceAddress,
							uint8 endpointAddress,
							usb_speed speed,
							UsbBusPipe::pipeDirection direction,
							size_t maxPacketSize,
							uint8 interval,
							int8 hubAddress, uint8 hubPort) = 0;
protected:
	~UsbBusManager() = default;
};


class UsbStack {
public:
	virtual bool			Lock() = 0;
	virtual void			Unlock() = 0;

	virtual status_t		AllocateChunk(void **logicalAddress,
								phys_addr_t *physicalAddress,
								size_t size) = 0;
	virtual status_t		FreeChunk(void *logicalAddress,
								phys_addr_t physicalAddress,
								size_t size) = 0;
	virtual area_id			AllocateArea(void **logicalAddress,
								phys_addr_t *physicalAddress,
								size_t size, const char *name) = 0;
protected:
	~UsbStack() = default;
};


class UsbHostController {
public:
	static inline const char ifaceName[] = "busses/usb/device";

	virtual void			SetBusManager(UsbBusManager* busManager) = 0;

	virtual	UsbBusDevice*	AllocateDevice(UsbBusDevice* parent,
								int8 hubAddress, uint8 hubPort,
								usb_speed speed) = 0;
	virtual void			FreeDevice(UsbBusDevice* device) = 0;

	virtual	status_t		Start() = 0;
	virtual	status_t		Stop() = 0;

	virtual	status_t		StartDebugTransfer(UsbBusTransfer* transfer) = 0;
	virtual	status_t		CheckDebugTransfer(UsbBusTransfer* transfer) = 0;
	virtual	void			CancelDebugTransfer(UsbBusTransfer* transfer) = 0;

	virtual	status_t		SubmitTransfer(UsbBusTransfer* transfer) = 0;
	virtual	status_t		CancelQueuedTransfers(UsbBusPipe* pipe, bool force) = 0;

	virtual	status_t		NotifyPipeChange(UsbBusPipe* pipe, usb_change change) = 0;

	virtual	const char*		TypeName() const = 0;

protected:
	~UsbHostController() = default;
};
