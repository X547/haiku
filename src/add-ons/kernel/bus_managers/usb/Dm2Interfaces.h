#pragma once

#include <dm2/bus/USB.h>


class Object;
class Device;
class Hub;
class Interface;
class Pipe;


class UsbObjectImpl final: public UsbObject {
public:
	UsbObjectImpl(Object& base): fBase(base) {}

	status_t SetFeature(uint16 selector) final;
	status_t ClearFeature(uint16 selector) final;
	status_t GetStatus(uint16 *status) final;

private:
	Object& fBase;
};


class UsbDeviceImpl final: public BusDriver, public UsbDevice {
public:
	UsbDeviceImpl(Device& base): fBase(base) {}

	// BusDriver
	void* QueryInterface(const char* name) final;

	UsbObject 	*GetObject() final;

	const usb_device_descriptor
						*GetDeviceDescriptor() final;

	const usb_configuration_info
						*GetNthConfiguration(uint32 index) final;

	const usb_configuration_info
						*GetConfiguration() final;

	status_t	SetConfiguration(const usb_configuration_info *configuration) final;

	status_t	SetAltInterface(const usb_interface_info *interface) final;

	status_t	GetDescriptor(
							uint8 descriptorType, uint8 index,
							uint16 languageID, void *data,
							size_t dataLength,
							size_t *actualLength) final;

	status_t	SendRequest(
							uint8 requestType, uint8 request,
							uint16 value, uint16 index,
							uint16 length, void *data,
							size_t *actualLength) final;

	status_t	QueueRequest(
							uint8 requestType, uint8 request,
							uint16 value, uint16 index,
							uint16 length, void *data,
							usb_callback_func callback,
							void *callbackCookie) final;

	status_t	CancelQueuedRequests() final;

private:
	Device& fBase;
};


class UsbHubImpl final: public UsbHub {
public:
	UsbHubImpl(Hub& base): fBase(base) {}

	UsbDevice 	*GetDevice() final;

	status_t	ResetPort(uint8 portIndex) final;
	status_t	DisablePort(uint8 portIndex) final;

private:
	Hub& fBase;
};


class UsbInterfaceImpl final: public UsbInterface {
public:
	UsbInterfaceImpl(Interface& base): fBase(base) {}
	Interface* Base() {return &fBase;}

	UsbObject *GetObject() final;

private:
	Interface& fBase;
};


class UsbPipeImpl final: public UsbPipe {
public:
	UsbPipeImpl(Pipe& base): fBase(base) {}
	Pipe* Base() {return &fBase;}

	UsbObject 	*GetObject() final;

	status_t	QueueInterrupt(
							void *data, size_t dataLength,
							usb_callback_func callback,
							void *callbackCookie) final;

	status_t	QueueBulk(
							void *data, size_t dataLength,
							usb_callback_func callback,
							void *callbackCookie) final;

	status_t	QueueBulkV(
							iovec *vector, size_t vectorCount,
							usb_callback_func callback,
							void *callbackCookie) final;

	status_t	QueueBulkVPhysical(
							physical_entry *vectors, size_t vectorCount,
							usb_callback_func callback,
							void *callbackCookie) final;

	status_t	QueueIsochronous(
							void *data, size_t dataLength,
							usb_iso_packet_descriptor *packetDesc,
							uint32 packetCount,
							uint32 *startingFrameNumber,
							uint32 flags,
							usb_callback_func callback,
							void *callbackCookie) final;


	status_t	SetPipePolicy(
							uint8 maxNumQueuedPackets,
							uint16 maxBufferDurationMS,
							uint16 sampleSize) final;

	status_t	CancelQueuedTransfers() final;

private:
	Pipe& fBase;
};
