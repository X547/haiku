#include "Dm2Interfaces.h"

#include "usb_private.h"


// #pragma mark - UsbObjectImpl

status_t
UsbObjectImpl::SetFeature(uint16 selector)
{
	return fBase.SetFeature(selector);;
}


status_t
UsbObjectImpl::ClearFeature(uint16 selector)
{
	return fBase.ClearFeature(selector);;
}


status_t
UsbObjectImpl::GetStatus(uint16 *status)
{
	return fBase.GetStatus(status);;
}


// #pragma mark - UsbDeviceImpl

void*
UsbDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, UsbDevice::ifaceName) == 0)
		return static_cast<UsbDevice*>(this);

	return NULL;
}


UsbObject *
UsbDeviceImpl::GetObject()
{
	return fBase.GetObjectIface();
}


const usb_device_descriptor *
UsbDeviceImpl::GetDeviceDescriptor()
{
	return fBase.DeviceDescriptor();
}


const usb_configuration_info *
UsbDeviceImpl::GetNthConfiguration(uint32 index)
{
	return fBase.ConfigurationAt(index);
}


const usb_configuration_info *
UsbDeviceImpl::GetConfiguration()
{
	return fBase.Configuration();
}


status_t
UsbDeviceImpl::SetConfiguration(const usb_configuration_info *configuration)
{
	return fBase.SetConfiguration(configuration);
}


status_t
UsbDeviceImpl::SetAltInterface(const usb_interface_info *interface)
{
	return fBase.SetAltInterface(interface);
}


status_t
UsbDeviceImpl::GetDescriptor(
	uint8 descriptorType, uint8 index,
	uint16 languageID, void *data,
	size_t dataLength,
	size_t *actualLength)
{
	return fBase.GetDescriptor(descriptorType, index, languageID, data, dataLength, actualLength);
}


status_t
UsbDeviceImpl::SendRequest(
	uint8 requestType, uint8 request,
	uint16 value, uint16 index,
	uint16 length, void *data,
	size_t *actualLength)
{
	return fBase.DefaultPipe()->SendRequest(requestType, request, value, index, length, data, length, actualLength);
}


status_t
UsbDeviceImpl::QueueRequest(
	uint8 requestType, uint8 request,
	uint16 value, uint16 index,
	uint16 length, void *data,
	usb_callback_func callback,
	void *callbackCookie)
{
	return fBase.DefaultPipe()->QueueRequest(requestType, request, value, index, length, data, length, callback, callbackCookie);
}


status_t
UsbDeviceImpl::CancelQueuedRequests()
{
	return fBase.DefaultPipe()->CancelQueuedTransfers(false);
}


// #pragma mark - UsbHubImpl

UsbDevice *
UsbHubImpl::GetDevice()
{
	return fBase.GetDeviceIface();
}


status_t
UsbHubImpl::ResetPort(uint8 portIndex)
{
	return fBase.ResetPort(portIndex);
}


status_t
UsbHubImpl::DisablePort(uint8 portIndex)
{
	return fBase.DisablePort(portIndex);
}


// #pragma mark - UsbInterfaceImpl

UsbDevice *
UsbInterfaceImpl::GetDevice()
{
	Device *device = static_cast<Device*>(fBase.Parent());
	return device->GetDeviceIface();
};


UsbObject *
UsbInterfaceImpl::GetObject()
{
	return fBase.GetObjectIface();
};


status_t
UsbInterfaceImpl::GetDescriptor(
	uint8 descriptorType, uint8 index,
	void *data, size_t dataLength,
	size_t *actualLength)
{
	Device *device = static_cast<Device*>(fBase.Parent());
	int8 interfaceIndex = fBase.InterfaceIndex();
	return device->DefaultPipe()->SendRequest(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
		USB_REQUEST_GET_DESCRIPTOR, (descriptorType << 8) | index,
		interfaceIndex, dataLength, data, dataLength, actualLength);
}


status_t
UsbInterfaceImpl::SendRequest(
	uint8 requestType, uint8 request,
	uint16 value,
	uint16 length, void *data,
	size_t *actualLength)
{
	Device *device = static_cast<Device*>(fBase.Parent());
	int8 index = fBase.InterfaceIndex();
	return device->DefaultPipe()->SendRequest(requestType, request, value, index, length, data, length, actualLength);
}


status_t
UsbInterfaceImpl::QueueRequest(
	uint8 requestType, uint8 request,
	uint16 value,
	uint16 length, void *data,
	usb_callback_func callback,
	void *callbackCookie)
{
	Device *device = static_cast<Device*>(fBase.Parent());
	int8 index = fBase.InterfaceIndex();
	return device->DefaultPipe()->QueueRequest(requestType, request, value, index, length, data, length, callback, callbackCookie);
}


// #pragma mark - UsbPipeImpl

UsbObject *
UsbPipeImpl::GetObject()
{
	return fBase.GetObjectIface();
}


status_t
UsbPipeImpl::QueueInterrupt(
	void *data, size_t dataLength,
	usb_callback_func callback,
	void *callbackCookie)
{
	if ((fBase.Type() & USB_OBJECT_INTERRUPT_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return static_cast<InterruptPipe&>(fBase).QueueInterrupt(data, dataLength, callback, callbackCookie);
}


status_t
UsbPipeImpl::QueueBulk(
	void *data, size_t dataLength,
	usb_callback_func callback,
	void *callbackCookie)
{
	if ((fBase.Type() & USB_OBJECT_BULK_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return static_cast<BulkPipe&>(fBase).QueueBulk(data, dataLength, callback, callbackCookie);
}


status_t
UsbPipeImpl::QueueBulkV(
	iovec *vector, size_t vectorCount,
	usb_callback_func callback,
	void *callbackCookie)
{
	if ((fBase.Type() & USB_OBJECT_BULK_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return static_cast<BulkPipe&>(fBase).QueueBulkV(vector, vectorCount, callback, callbackCookie);
}


status_t
UsbPipeImpl::QueueBulkVPhysical(
	physical_entry *vectors, size_t vectorCount,
	usb_callback_func callback,
	void *callbackCookie)
{
	if ((fBase.Type() & USB_OBJECT_BULK_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return static_cast<BulkPipe&>(fBase).QueueBulkV(vectors, vectorCount, callback, callbackCookie);
}


status_t
UsbPipeImpl::QueueIsochronous(
	void *data, size_t dataLength,
	usb_iso_packet_descriptor *packetDesc,
	uint32 packetCount,
	uint32 *startingFrameNumber,
	uint32 flags,
	usb_callback_func callback,
	void *callbackCookie)
{
	if ((fBase.Type() & USB_OBJECT_ISO_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return static_cast<IsochronousPipe&>(fBase).QueueIsochronous(data, dataLength,
		packetDesc, packetCount, startingFrameNumber, flags, callback,
		callbackCookie);
}



status_t
UsbPipeImpl::SetPipePolicy(
	uint8 maxNumQueuedPackets,
	uint16 maxBufferDurationMS,
	uint16 sampleSize)
{
	if ((fBase.Type() & USB_OBJECT_ISO_PIPE) == 0)
		return B_DEV_INVALID_PIPE;

	return static_cast<IsochronousPipe&>(fBase).SetPipePolicy(maxNumQueuedPackets,
		maxBufferDurationMS, sampleSize);
}


status_t
UsbPipeImpl::CancelQueuedTransfers()
{
	return fBase.CancelQueuedTransfers(false);
}
