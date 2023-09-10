#include <string.h>
#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/USB.h>
#include <dm2/bus/HID.h>
#include <usb/USB_hid.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <ScopeExit.h>
#include <util/AutoLock.h>
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
	static void InputCallback(void *cookie, status_t status, void *data, size_t actualLength);

private:
	mutex fLock = MUTEX_INITIALIZER("usb_hid");
	DeviceNode* fNode;
	UsbDevice* fUsbDevice {};
	UsbInterface* fInterface {};
	UsbPipe* fInterruptPipe {};

	size_t fInputBufferSize {};
	size_t fInputActualSize {};
	ArrayDeleter<uint8> fInputBuffer;

	HidInputCallback* fCallback {};

	class HidDeviceImpl: public BusDriver, public HidDevice {
	public:
		HidDeviceImpl(UsbHidDriver& base): fBase(base) {}

		// BusDriver
		void* QueryInterface(const char* name) final;
		void DriverAttached(bool isAttached) final;

		// HidDevice
		status_t Reset() final;
		status_t RequestRead(uint32 size, uint8* data, HidInputCallback* callback) final;
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

	const usb_configuration_info* configuration = fUsbDevice->GetConfiguration();
	dprintf("  configuration: %p\n", configuration);
	if (configuration == NULL)
		return ENODEV;

	dprintf("  configuration->interface_count: %" B_PRIuSIZE "\n", configuration->interface_count);

	usb_interface_info* interface = configuration->interface[0].active;
	dprintf("  interface: %p\n", interface);
	fInterface = interface->handle;
	dprintf("  fInterface: %p\n", fInterface);

	for (size_t i = 0; i < interface->endpoint_count; i++) {
		usb_endpoint_info *endpoint = &interface->endpoint[i];
		if (endpoint == NULL)
			continue;

		if ((endpoint->descr->endpoint_address & USB_ENDPOINT_ADDR_DIR_IN)
			&& endpoint->descr->attributes == USB_ENDPOINT_ATTR_INTERRUPT) {
			fInterruptPipe = endpoint->handle;
			break;
		}
	}

	dprintf("  fInterruptPipe: %p\n", fInterruptPipe);
	if (fInterruptPipe == NULL)
		return ENODEV;


	// descriptor from the device.
	usb_hid_descriptor *hidDesc = NULL;

	const usb_interface_info* interfaceInfo = configuration->interface[0].active;
	for (size_t i = 0; i < interfaceInfo->generic_count; i++) {
		const usb_generic_descriptor &generic = interfaceInfo->generic[i]->generic;
		if (generic.descriptor_type == B_USB_HID_DESCRIPTOR_HID) {
			hidDesc = (usb_hid_descriptor *)&generic;
			break;
		}
	}

	if (hidDesc == NULL)
		return ENODEV;

	size_t reportDescLength = hidDesc->descriptor_info[0].descriptor_length;
	ArrayDeleter<uint8> reportDesc(new(std::nothrow) uint8[reportDescLength]);
	if (!reportDesc.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fInterface->GetDescriptor(
		B_USB_HID_DESCRIPTOR_REPORT, 0,
		&reportDesc[0], reportDescLength, &reportDescLength));

	dprintf("  reportDescLength: %" B_PRIuSIZE "\n", reportDescLength);

	fInputBufferSize = 128; // !!!
	fInputBuffer.SetTo(new(std::nothrow) uint8[fInputBufferSize]);
	if (!fInputBuffer.IsSet())
		return B_NO_MEMORY;

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "HID Device"}},
		{B_DEVICE_BUS,         B_STRING_TYPE, {.string = "hid"}},

		{HID_DEVICE_REPORT_DESC,     B_RAW_TYPE,    {.raw = {.data = &reportDesc[0], .length = reportDescLength}}},
		{HID_DEVICE_MAX_INPUT_SIZE,  B_UINT16_TYPE, {.ui16 = (uint16)fInputBufferSize}},
		{HID_DEVICE_MAX_OUTPUT_SIZE, B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_VENDOR,          B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_PRODUCT,         B_UINT16_TYPE, {.ui16 = 0}},
		{HID_DEVICE_VERSION,         B_UINT16_TYPE, {.ui16 = 0}},

		{}
	};

	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fHidDevice), attrs, NULL));

	return B_OK;
}


void
UsbHidDriver::InputCallback(void *cookie, status_t status, void *data, size_t actualLength)
{
	UsbHidDriver* driver = (UsbHidDriver*)cookie;

	MutexLocker lock(&driver->fLock);

#if 0
	dprintf("UsbHidDriver::InputCallback(%#" B_PRIx32 ", %" B_PRIuSIZE ")\n", status, actualLength);

	for (size_t i = 0; i < actualLength; i++) {
		dprintf(" %02x", ((uint8*)data)[i]);
		if (i == 15)
			dprintf("\n");
	}
	if (actualLength % 16 != 0)
		dprintf("\n");
#endif

	HidInputCallback* callback = driver->fCallback;
	driver->fCallback = NULL;
	lock.Unlock();

	callback->InputAvailable(status, (uint8*)data, actualLength);
}


// #pragma mark - BusDriver

void*
UsbHidDriver::HidDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, HidDevice::ifaceName) == 0)
		return static_cast<HidDevice*>(this);

	return NULL;
}


void
UsbHidDriver::HidDeviceImpl::DriverAttached(bool isAttached)
{
#if 0
	if (!isAttached)
		fBase.fInterruptPipe->CancelQueuedTransfers();
#endif
}


// #pragma mark - HidDevice

status_t
UsbHidDriver::HidDeviceImpl::Reset()
{
	return ENOSYS;
}


status_t
UsbHidDriver::HidDeviceImpl::RequestRead(uint32 size, uint8* data, HidInputCallback* callback)
{
	MutexLocker lock(&fBase.fLock);

	if (fBase.fCallback != NULL)
		return B_BUSY;

	fBase.fCallback = callback;

	return fBase.fInterruptPipe->QueueInterrupt(data, size, InputCallback, &fBase);
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
