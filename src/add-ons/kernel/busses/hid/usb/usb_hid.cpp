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

	ArrayDeleter<uint8> fReportDecriptor;

	class HidDeviceImpl: public BusDriver, public HidDevice {
	public:
		HidDeviceImpl(UsbHidDriver& base): fBase(base) {}

		// BusDriver
		status_t InitDriver(DeviceNode* node) final;
		const device_attr* Attributes() const final;
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

	size_t interfaceIndex = 0;
	size_t descriptorLength = 0;

	CHECK_RET(fUsbDevice->SendRequest(
		USB_REQTYPE_INTERFACE_IN | USB_REQTYPE_STANDARD,
		USB_REQUEST_GET_DESCRIPTOR,
		B_USB_HID_DESCRIPTOR_REPORT << 8, interfaceIndex, descriptorLength,
		&fReportDecriptor[0], &descriptorLength));

	CHECK_RET(fNode->RegisterNode(static_cast<BusDriver*>(&fHidDevice), NULL));

	return B_OK;
}


// #pragma mark - BusDriver

status_t
UsbHidDriver::HidDeviceImpl::InitDriver(DeviceNode* node)
{
	fAttrs.Add({B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "HID Device"}});
	fAttrs.Add({B_DEVICE_BUS,         B_STRING_TYPE, {.string = "hid"}});

	fAttrs.Add({HID_DEVICE_REPORT_DESC,     B_RAW_TYPE,    {.raw = {.data = &fBase.fReportDecriptor[0], .length = 0}}});
	fAttrs.Add({HID_DEVICE_MAX_INPUT_SIZE,  B_UINT16_TYPE, {.ui16 = 0}});
	fAttrs.Add({HID_DEVICE_MAX_OUTPUT_SIZE, B_UINT16_TYPE, {.ui16 = 0}});
	fAttrs.Add({HID_DEVICE_VENDOR,          B_UINT16_TYPE, {.ui16 = 0}});
	fAttrs.Add({HID_DEVICE_PRODUCT,         B_UINT16_TYPE, {.ui16 = 0}});
	fAttrs.Add({HID_DEVICE_VERSION,         B_UINT16_TYPE, {.ui16 = 0}});

	fAttrs.Add({});

	return B_OK;
}


const device_attr*
UsbHidDriver::HidDeviceImpl::Attributes() const
{
	return &fAttrs[0];
}

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
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::SetReport(uint8 reportType, uint8 reportId, uint32 size, const uint8* data)
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::GetIdle(uint8 reportId, uint16* idle)
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::SetIdle(uint8 reportId, uint16 idle)
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::GetProtocol(uint16* protocol)
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::SetProtocol(uint16 protocol)
{
	return ENOSYS;
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
