#include "Dm2BusInterfaces.h"

#include <AutoDeleter.h>

#include "usb_private.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


// #pragma mark - UsbBusDeviceImpl

void
UsbBusDeviceImpl::Free()
{
	delete &fBase;
}


UsbBusDevice *
UsbBusDeviceImpl::Parent()
{
	Object *object = fBase.Parent();
	if ((object->Type() & USB_OBJECT_DEVICE) != 0 || (object->Type() & USB_OBJECT_HUB) != 0)
		return static_cast<Device*>(object)->GetBusDeviceIface();

	return NULL;
}


int8
UsbBusDeviceImpl::DeviceAddress() const
{
	return fBase.DeviceAddress();
}


usb_speed
UsbBusDeviceImpl::Speed() const
{
	return fBase.Speed();
}


int8
UsbBusDeviceImpl::HubAddress() const
{
	return fBase.HubAddress();
}


uint8
UsbBusDeviceImpl::HubPort() const
{
	return fBase.HubPort();
}


void
UsbBusDeviceImpl::SetControllerCookie(void *cookie)
{
	fBase.SetControllerCookie(cookie);
}


void *UsbBusDeviceImpl::ControllerCookie() const
{
	return fBase.ControllerCookie();
}


// #pragma mark - UsbBusPipeImpl

void
UsbBusPipeImpl::Free()
{
	delete &fBase;
}


UsbBusDevice *
UsbBusPipeImpl::GetDevice()
{
	Object *object = fBase.Parent();
	if ((object->Type() & USB_OBJECT_DEVICE) != 0 || (object->Type() & USB_OBJECT_HUB) != 0)
		return static_cast<Device*>(object)->GetBusDeviceIface();

	return NULL;
}


usb_pipe_type
UsbBusPipeImpl::Type() const
{
	uint32 pipeMask
		= USB_OBJECT_CONTROL_PIPE
		| USB_OBJECT_INTERRUPT_PIPE
		| USB_OBJECT_BULK_PIPE
		| USB_OBJECT_ISO_PIPE;

	switch (fBase.Type() & pipeMask) {
		case USB_OBJECT_CONTROL_PIPE:
			return USB_PIPE_CONTROL;
		case USB_OBJECT_INTERRUPT_PIPE:
			return USB_PIPE_INTERRUPT;
		case USB_OBJECT_BULK_PIPE:
			return USB_PIPE_BULK;
		case USB_OBJECT_ISO_PIPE:
			return USB_PIPE_ISO;
	}
	return USB_PIPE_INVALID;
}


int8
UsbBusPipeImpl::DeviceAddress() const
{
	return fBase.DeviceAddress();
}


usb_speed
UsbBusPipeImpl::Speed() const
{
	return fBase.Speed();
}


UsbBusPipe::pipeDirection
UsbBusPipeImpl::Direction() const
{
	return (pipeDirection)(int)fBase.Direction();
}


uint8
UsbBusPipeImpl::EndpointAddress() const
{
	return fBase.EndpointAddress();
}


size_t
UsbBusPipeImpl::MaxPacketSize() const
{
	return fBase.MaxPacketSize();
}


uint8
UsbBusPipeImpl::Interval() const
{
	return fBase.Interval();
}


uint8
UsbBusPipeImpl::MaxBurst() const
{
	return fBase.MaxBurst();
}


uint16
UsbBusPipeImpl::BytesPerInterval() const
{
	return fBase.BytesPerInterval();
}


void
UsbBusPipeImpl::SetHubInfo(int8 address, uint8 port)
{
	fBase.SetHubInfo(address, port);
}


int8
UsbBusPipeImpl::HubAddress() const
{
	return fBase.HubAddress();
}


uint8
UsbBusPipeImpl::HubPort() const
{
	return fBase.HubPort();
}


bool
UsbBusPipeImpl::DataToggle() const
{
	return fBase.DataToggle();
}


void
UsbBusPipeImpl::SetDataToggle(bool toggle)
{
	fBase.SetDataToggle(toggle);
}


status_t
UsbBusPipeImpl::SubmitTransfer(UsbBusTransfer *transfer)
{
	return fBase.SubmitTransfer(static_cast<UsbBusTransferImpl*>(transfer)->Base());
}


status_t
UsbBusPipeImpl::CancelQueuedTransfers(bool force)
{
	return fBase.CancelQueuedTransfers(force);
}


void
UsbBusPipeImpl::SetControllerCookie(void *cookie)
{
	fBase.SetControllerCookie(cookie);
}


void *UsbBusPipeImpl::ControllerCookie() const
{
	return fBase.ControllerCookie();
}


status_t
UsbBusPipeImpl::SendRequest(uint8 requestType,
	uint8 request, uint16 value,
	uint16 index, uint16 length,
	void *data, size_t dataLength,
	size_t *actualLength)
{
	return static_cast<ControlPipe&>(fBase).SendRequest(requestType, request, value, index, length, data, dataLength, actualLength);
}


// #pragma mark - UsbBusTransferImpl

void
UsbBusTransferImpl::Free()
{
	delete &fBase;
}


UsbBusPipe *
UsbBusTransferImpl::TransferPipe() const
{
	return fBase.TransferPipe()->GetBusPipeIface();
}


usb_request_data *
UsbBusTransferImpl::RequestData() const
{
	return fBase.RequestData();
}


usb_isochronous_data *
UsbBusTransferImpl::IsochronousData() const
{
	return fBase.IsochronousData();
}


uint8 *
UsbBusTransferImpl::Data() const
{
	return fBase.Data();
}

size_t
UsbBusTransferImpl::DataLength() const
{
	return fBase.DataLength();
}


bool
UsbBusTransferImpl::IsPhysical() const
{
	return fBase.IsPhysical();
}


generic_io_vec *
UsbBusTransferImpl::Vector()
{
	return fBase.Vector();
}


size_t
UsbBusTransferImpl::VectorCount() const
{
	return fBase.VectorCount();
}


uint16
UsbBusTransferImpl::Bandwidth() const
{
	return fBase.Bandwidth();
}


bool
UsbBusTransferImpl::IsFragmented() const
{
	return fBase.IsFragmented();
}


void
UsbBusTransferImpl::AdvanceByFragment(size_t actualLength)
{
	return fBase.AdvanceByFragment(actualLength);
}


size_t
UsbBusTransferImpl::FragmentLength() const
{
	return fBase.FragmentLength();
}


status_t
UsbBusTransferImpl::InitKernelAccess()
{
	return fBase.InitKernelAccess();
}


status_t
UsbBusTransferImpl::PrepareKernelAccess()
{
	return fBase.PrepareKernelAccess();
}


void
UsbBusTransferImpl::SetCallback(usb_callback_func callback, void *cookie)
{
	fBase.SetCallback(callback, cookie);
}


usb_callback_func
UsbBusTransferImpl::Callback() const
{
	return fBase.Callback();
}


void *
UsbBusTransferImpl::CallbackCookie() const
{
	return fBase.CallbackCookie();
}


void
UsbBusTransferImpl::Finished(uint32 status, size_t actualLength)
{
	fBase.Finished(status, actualLength);
}


// #pragma mark - UsbBusManagerImpl

void
UsbBusManagerImpl::Free()
{
	delete &fBase;
}


bool
UsbBusManagerImpl::Lock()
{
	return fBase.Lock();
}


void
UsbBusManagerImpl::Unlock()
{
	fBase.Unlock();
}


int32
UsbBusManagerImpl::ID()
{
	return Stack::Instance().IndexOfBusManager(&fBase);
}


int8
UsbBusManagerImpl::AllocateAddress()
{
	return fBase.AllocateAddress();
}


void
UsbBusManagerImpl::FreeAddress(int8 address)
{
	fBase.FreeAddress(address);
}


UsbBusDevice *
UsbBusManagerImpl::GetRootHub() const
{
	return fBase.GetRootHub()->GetBusDeviceIface();
}


void
UsbBusManagerImpl::SetRootHub(UsbBusDevice* hub)
{
	return fBase.SetRootHub(static_cast<Hub*>(static_cast<UsbBusDeviceImpl*>(hub)->Base()));
}


status_t
UsbBusManagerImpl::CreateDevice(UsbBusDevice*& outDevice, UsbBusDevice* parent, int8 hubAddress,
	uint8 hubPort,
	usb_device_descriptor& desc,
	int8 deviceAddress,
	usb_speed speed, bool isRootHub,
	void *controllerCookie)
{
	ObjectDeleter<Device> device(new(std::nothrow) Device(parent == NULL ? fBase.RootObject() : static_cast<UsbBusDeviceImpl*>(parent)->Base(), hubAddress, hubPort,
		desc, deviceAddress, speed, isRootHub, controllerCookie));

	if (!device.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(device->InitCheck());

	outDevice = device->GetBusDeviceIface();
	device.Detach();

	return B_OK;
}


status_t
UsbBusManagerImpl::CreateHub(UsbBusDevice*& outDevice, UsbBusDevice* parent, int8 hubAddress,
	uint8 hubPort,
	usb_device_descriptor& desc,
	int8 deviceAddress,
	usb_speed speed, bool isRootHub,
	void* controllerCookie)
{
	ObjectDeleter<Device> device(new(std::nothrow) Hub(parent == NULL ? fBase.RootObject() : static_cast<UsbBusDeviceImpl*>(parent)->Base(), hubAddress, hubPort,
		desc, deviceAddress, speed, isRootHub, controllerCookie));

	if (!device.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(device->InitCheck());

	outDevice = device->GetBusDeviceIface();
	device.Detach();

	return B_OK;
}


status_t
UsbBusManagerImpl::CreateControlPipe(UsbBusPipe*& outPipe, UsbBusDevice* parent,
	int8 deviceAddress,
	uint8 endpointAddress,
	usb_speed speed,
	UsbBusPipe::pipeDirection direction,
	size_t maxPacketSize,
	uint8 interval,
	int8 hubAddress, uint8 hubPort)
{
	ObjectDeleter<ControlPipe> pipe(new(std::nothrow) ControlPipe(static_cast<UsbBusDeviceImpl*>(parent)->Base()));

	if (!pipe.IsSet())
		return B_NO_MEMORY;

	pipe->InitCommon(deviceAddress, endpointAddress, speed, (Pipe::pipeDirection)(int)direction,
		maxPacketSize, interval, hubAddress, hubPort);

	outPipe = pipe->GetBusPipeIface();
	pipe.Detach();

	return B_OK;
}


// #pragma mark - UsbStackImpl

bool
UsbStackImpl::Lock()
{
	return fBase.Lock();
}


void
UsbStackImpl::Unlock()
{
	fBase.Unlock();
}


status_t
UsbStackImpl::AllocateChunk(void **logicalAddress, phys_addr_t *physicalAddress, size_t size)
{
	return fBase.AllocateChunk(logicalAddress, physicalAddress, size);
}


status_t
UsbStackImpl::FreeChunk(void *logicalAddress, phys_addr_t physicalAddress, size_t size)
{
	return fBase.FreeChunk(logicalAddress, physicalAddress, size);
}


area_id
UsbStackImpl::AllocateArea(void **logicalAddress,
	phys_addr_t *physicalAddress,
	size_t size, const char *name)
{
	return fBase.AllocateArea(logicalAddress, physicalAddress, size, name);
}
