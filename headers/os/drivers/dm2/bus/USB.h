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
	status_t SetFeature(uint16 selector);
	status_t ClearFeature(uint16 selector);
	status_t GetStatus(uint16 *status);

protected:
	~UsbObject() = default;
};


class UsbDevice: public UsbObject {
public:
	static inline const char ifaceName[] = "bus_managers/usb/device";

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
	~UsbDevice() = default;
};


class UsbInterface: public UsbObject {
protected:
	~UsbInterface() = default;
};


class UsbPipe: public UsbObject {
public:
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


// TODO: host controller interface
