#pragma once

#include <dm2/bus/USB.h>


class Device;
class Pipe;
class Transfer;
class BusManager;
class Stack;


class UsbBusDeviceImpl: public UsbBusDevice {
public:
							UsbBusDeviceImpl(Device& base): fBase(base) {}
	Device* 				Base() {return &fBase;}
	void					Free() final;

	UsbBusDevice *			Parent() final;
	int8					DeviceAddress() const final;
	usb_speed				Speed() const  final;
	int8					HubAddress() const final;
	uint8					HubPort() const final;
	void *					ControllerCookie() const final;

private:
	Device& fBase;
};


class UsbBusPipeImpl: public UsbBusPipe {
public:
							UsbBusPipeImpl(Pipe& base): fBase(base) {}

	UsbBusDevice *			GetDevice() final;
	usb_pipe_type			Type() const final;

	int8					DeviceAddress() const final;
	usb_speed				Speed() const final;
	pipeDirection			Direction() const final;
	uint8					EndpointAddress() const final;
	size_t					MaxPacketSize() const final;
	uint8					Interval() const final;
	uint8					MaxBurst() const final;
	uint16					BytesPerInterval() const final;
	void					SetHubInfo(int8 address, uint8 port) final;
	int8					HubAddress() const final;
	uint8					HubPort() const final;
	bool					DataToggle() const final;
	void					SetDataToggle(bool toggle) final;

	status_t				SubmitTransfer(UsbBusTransfer *transfer) final;
	status_t				CancelQueuedTransfers(bool force) final;

	void					SetControllerCookie(void *cookie) final;
	void *					ControllerCookie() const final;

	status_t				SendRequest(uint8 requestType,
								uint8 request, uint16 value,
								uint16 index, uint16 length,
								void *data, size_t dataLength,
								size_t *actualLength) final;
private:
	Pipe& fBase;
};


class UsbBusTransferImpl: public UsbBusTransfer {
public:
							UsbBusTransferImpl(Transfer& base): fBase(base) {}
	Transfer* 				Base() {return &fBase;}
	void					Free() final;

	UsbBusPipe *			TransferPipe() const final;
	usb_request_data *		RequestData() const final;
	usb_isochronous_data *	IsochronousData() const final;
	uint8 *					Data() const final;
	size_t					DataLength() const final;
	bool					IsPhysical() const final;
	generic_io_vec *		Vector() final;
	size_t					VectorCount() const final;
	uint16					Bandwidth() const final;
	bool					IsFragmented() const final;
	void					AdvanceByFragment(size_t actualLength) final;
	size_t					FragmentLength() const final;
	status_t				InitKernelAccess() final;
	status_t				PrepareKernelAccess() final;
	void					SetCallback(usb_callback_func callback, void *cookie) final;
	usb_callback_func		Callback() const final;
	void *					CallbackCookie() const final;
	void					Finished(uint32 status, size_t actualLength) final;

private:
	Transfer& fBase;
};


class UsbBusManagerImpl: public UsbBusManager {
public:
							UsbBusManagerImpl(BusManager& base): fBase(base) {}
	void					Free() final;

	bool					Lock() final;
	void					Unlock() final;

	int32					ID() final;

	int8					AllocateAddress() final;
	void					FreeAddress(int8 address) final;

	UsbBusDevice *			GetRootHub() const final;
	void					SetRootHub(UsbBusDevice* hub) final;

	status_t				CreateDevice(UsbBusDevice*& outDevice, UsbBusDevice* parent, int8 hubAddress,
								uint8 hubPort,
								int8 deviceAddress,
								usb_speed speed, bool isRootHub,
								void *controllerCookie = NULL) final;

private:
	BusManager& fBase;
};


class UsbStackImpl: public UsbStack {
public:
							UsbStackImpl(Stack& base): fBase(base) {}

	bool					Lock() final;
	void					Unlock() final;

	status_t				AllocateChunk(void **logicalAddress,
								phys_addr_t *physicalAddress,
								size_t size) final;
	status_t				FreeChunk(void *logicalAddress,
								phys_addr_t physicalAddress,
								size_t size) final;
	area_id					AllocateArea(void **logicalAddress,
								phys_addr_t *physicalAddress,
								size_t size, const char *name) final;
private:
	Stack& fBase;
};
