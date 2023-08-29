#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/USB.h>
#include <dm2/bus/HID.h>
#include <usb/USB_hid.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <ScopeExit.h>
#include <util/Vector.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define USB_HID_DRIVER_MODULE_NAME "busses/hid/usb_hid/driver/v1"


class UsbHidDriver: public DeviceDriver {
public:
	UsbHidDriver(DeviceNode* node): fNode(node), fHidDevice(*this) {}
	virtual ~UsbHidDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	DeviceNode* fNode;
	UsbDevice* fUsbDevice {};
	UsbInterface* fInterface {};
	UsbPipe* fInterruptPipe {};

	ArrayDeleter<uint8> fReportDecriptor;

	ArrayDeleter<uint8> fInputBuffer;

	class HidDeviceImpl: public BusDriver, public HidDevice {
	public:
		HidDeviceImpl(UsbHidDriver& base): fBase(base) {}

		// BusDriver
		void* QueryInterface(const char* name) final;

		// HidDevice
		void SetCallback(HidDeviceCallback* callback) final;
		status_t Reset() final;
		status_t Read(uint32 size, uint8* data) final;
		status_t Write(uint32 size, const uint8* data) final;
		status_t GetReport(uint8 reportType, uint8 reportId, uint32 size, uint8 *data) final;
		status_t SetReport(uint8 reportType, uint8 reportId, uint32 size, const uint8* data) final;
		status_t GetIdle(uint8 reportId, uint16* idle) final;
		status_t SetIdle(uint8 reportId, uint16 idle) final;
		status_t GetProtocol(uint16* protocol) final;
		status_t SetProtocol(uint16 protocol) final;
		status_t SetPower(uint8 power) final;

	public:
		UsbHidDriver& fBase;
		Vector<device_attr> fAttrs;
		HidDeviceCallback* fCallback {};
	} fHidDevice;
};


UsbHidDriver::~UsbHidDriver()
{
}


status_t
UsbHidDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<UsbHidDriver> driver(new(std::nothrow) UsbHidDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
UsbHidDriver::Init()
{
	dprintf("UsbHidDriver::Init()\n");

	fUsbDevice = fNode->QueryBusInterface<UsbDevice>();

	// TODO: implement

	size_t descriptorLength = 0;

	CHECK_RET(fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
		USB_REQUEST_GET_DESCRIPTOR,
		B_USB_HID_DESCRIPTOR_REPORT << 8, descriptorLength,
		&fReportDecriptor[0], &descriptorLength));


	//status_t result = fInterruptPipe->QueueInterrupt(fInputBuffer.Get(), fTransferBufferSize, _TransferCallback, this);


	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "HID Device"}},
		{B_DEVICE_BUS,         B_STRING_TYPE, {.string = "hid"}},

		{HID_DEVICE_REPORT_DESC,     B_RAW_TYPE,    {.raw = {.data = &fReportDecriptor[0], .length = 0}}},
		{HID_DEVICE_MAX_INPUT_SIZE,  B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_MAX_OUTPUT_SIZE, B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_VENDOR,          B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_PRODUCT,         B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_VERSION,         B_UINT16_TYPE, {.ui16 = 0}},

		{}
	};

	CHECK_RET(fNode->RegisterNode(this, static_cast<BusDriver*>(&fHidDevice), attrs, NULL));

	return B_OK;
}


// #pragma mark - BusDriver

void*
UsbHidDriver::HidDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, HidDevice::ifaceName) == 0)
		return static_cast<HidDevice*>(this);

	return NULL;
}


// #pragma mark - HidDevice

void
UsbHidDriver::HidDeviceImpl::SetCallback(HidDeviceCallback* callback)
{
	fCallback = callback;
}


status_t
UsbHidDriver::HidDeviceImpl::Reset()
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::Read(uint32 size, uint8* data)
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::Write(uint32 size, const uint8* data)
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::GetReport(uint8 reportType, uint8 reportId, uint32 size, uint8 *data)
{
	size_t actualLength = 0;
	return fBase.fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_GET_REPORT, (reportType << 8) | reportId,
		size, data, &actualLength);

	return B_OK;
}


status_t
UsbHidDriver::HidDeviceImpl::SetReport(uint8 reportType, uint8 reportId, uint32 size, const uint8* data)
{
	size_t actualLength;
	return fBase.fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_REPORT, (reportType << 8) | reportId,
		size, (void*)data, &actualLength);
}


status_t
UsbHidDriver::HidDeviceImpl::GetIdle(uint8 reportId, uint16* outIdle)
{
	uint8 idle;
	size_t actualLength = 0;
	CHECK_RET(fBase.fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_GET_IDLE, reportId,
		sizeof(idle), &idle, &actualLength));
	if (actualLength != sizeof(idle))
		return B_BAD_VALUE;

	*outIdle = idle;
	return B_OK;
}


status_t
UsbHidDriver::HidDeviceImpl::SetIdle(uint8 reportId, uint16 idle)
{
	return fBase.fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_IDLE, (idle << 8) + reportId,
		0, NULL, NULL);
}


status_t
UsbHidDriver::HidDeviceImpl::GetProtocol(uint16* outProtocol)
{
	uint8 protocol;
	size_t actualLength = 0;
	CHECK_RET(fBase.fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_GET_PROTOCOL, 0,
		sizeof(protocol), &protocol, &actualLength));
	if (actualLength != sizeof(protocol))
		return B_BAD_VALUE;

	*outProtocol = protocol;
	return B_OK;
}


status_t
UsbHidDriver::HidDeviceImpl::SetProtocol(uint16 protocol)
{
	return fBase.fInterface->SendRequest(
		USB_REQTYPE_INTERFACE_OUT | USB_REQTYPE_CLASS,
		B_USB_REQUEST_HID_SET_PROTOCOL, protocol,
		0, NULL, NULL);
}


status_t
UsbHidDriver::HidDeviceImpl::SetPower(uint8 power)
{
	return ENOSYS;
}


static driver_module_info sUsbHidDriverModule = {
	.info = {
		.name = USB_HID_DRIVER_MODULE_NAME,
	},
	.probe = UsbHidDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sUsbHidDriverModule,
	NULL
};
