#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/I2C.h>
#include <dm2/bus/FDT.h>
#include <dm2/bus/HID.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>
#include <ScopeExit.h>
#include <util/Vector.h>

#include "I2CHIDProtocol.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define I2C_HID_DRIVER_MODULE_NAME "busses/hid/i2c_hid/driver/v1"


class I2cHidDriver: public DeviceDriver {
public:
	I2cHidDriver(DeviceNode* node): fNode(node), fHidDevice(*this) {}
	virtual ~I2cHidDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	status_t ExecCommand(i2c_op op, uint8* cmd, size_t cmdLength, uint8* buffer, size_t bufferLength);
	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};
	I2cBus* fI2cBus {};

	uint32 fDeviceAddress {};
	uint16 fDescriptorAddress {};
	long fIrqVector = -1;

	i2c_hid_descriptor fDescriptor {};
	ArrayDeleter<uint8> fReportDecriptor;

	class HidDeviceImpl: public BusDriver, public HidDevice {
	public:
		HidDeviceImpl(I2cHidDriver& base): fBase(base) {}

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
		I2cHidDriver& fBase;
		Vector<device_attr> fAttrs;
		HidDeviceCallback* fCallback {};
	} fHidDevice;
};


I2cHidDriver::~I2cHidDriver()
{
	if (fIrqVector >= 0)
		remove_io_interrupt_handler(fIrqVector, HandleInterrupt, this);
}


status_t
I2cHidDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<I2cHidDriver> driver(new(std::nothrow) I2cHidDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
I2cHidDriver::Init()
{
	dprintf("I2cHidDriver::Init()\n");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();
	DeviceNodePutter i2cBusNode(fNode->GetParent());
	fI2cBus = i2cBusNode->QueryDriverInterface<I2cBus>();

	int attrLen;
	const void *attr = fFdtDevice->GetProp("reg", &attrLen);
	if (attr == NULL || attrLen != 4)
		return B_ERROR;
	fDeviceAddress = B_BENDIAN_TO_HOST_INT32(*(const uint32*)attr);

	attr = fFdtDevice->GetProp("hid-descr-addr", &attrLen);
	if (attr == NULL || attrLen != 4)
		return B_ERROR;
	fDescriptorAddress = B_BENDIAN_TO_HOST_INT32(*(const uint32*)attr);

	uint64 val;
	if (!fFdtDevice->GetInterrupt(0, NULL, &val))
		return B_ERROR;
	fIrqVector = val;

	install_io_interrupt_handler(fIrqVector, HandleInterrupt, this, B_DEFERRED_COMPLETION);

	dprintf("  fDeviceAddress: %" B_PRIu32 "\n", fDeviceAddress);
	dprintf("  fDescriptorAddress: %" B_PRIu32 "\n", fDescriptorAddress);

	CHECK_RET(ExecCommand(I2C_OP_READ_STOP, (uint8*)&fDescriptorAddress, sizeof(fDescriptorAddress), (uint8*)&fDescriptor, sizeof(fDescriptor)));
	dprintf("  fDescriptor.wHIDDescLength: %" B_PRIu16 "\n", fDescriptor.wHIDDescLength);
	dprintf("  fDescriptor.wReportDescLength: %" B_PRIu16 "\n", fDescriptor.wReportDescLength);
	dprintf("  fDescriptor.wMaxInputLength: %" B_PRIu16 "\n", fDescriptor.wMaxInputLength);
	dprintf("  fDescriptor.wMaxOutputLength: %" B_PRIu16 "\n", fDescriptor.wMaxOutputLength);

	fReportDecriptor.SetTo(new(std::nothrow) uint8[fDescriptor.wReportDescLength]);
	if (!fReportDecriptor.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(ExecCommand(
		I2C_OP_READ_STOP,
		(uint8*)&fDescriptor.wReportDescRegister, sizeof(fDescriptor.wReportDescRegister),
		&fReportDecriptor[0], fDescriptor.wReportDescLength));

	CHECK_RET(fNode->RegisterNode(static_cast<BusDriver*>(&fHidDevice), NULL));

	return B_OK;
}


status_t
I2cHidDriver::ExecCommand(i2c_op op, uint8* cmd, size_t cmdLength, uint8* buffer, size_t bufferLength)
{
	CHECK_RET(fI2cBus->AcquireBus());
	ScopeExit busReleaser([this]() {
		fI2cBus->ReleaseBus();
	});

	CHECK_RET(fI2cBus->ExecCommand(op, fDeviceAddress, cmd, cmdLength, buffer, bufferLength));

	return B_OK;
}


int32
I2cHidDriver::HandleInterrupt(void* arg)
{
	return static_cast<I2cHidDriver*>(arg)->HandleInterruptInt();
}


int32
I2cHidDriver::HandleInterruptInt()
{
	if (fHidDevice.fCallback != NULL)
		fHidDevice.fCallback->InputAvailable();

	return B_HANDLED_INTERRUPT;
}


// #pragma mark - BusDriver

status_t
I2cHidDriver::HidDeviceImpl::InitDriver(DeviceNode* node)
{
	fAttrs.Add({B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "HID Device"}});
	fAttrs.Add({B_DEVICE_BUS,         B_STRING_TYPE, {.string = "hid"}});

	fAttrs.Add({HID_DEVICE_REPORT_DESC,     B_RAW_TYPE,    {.raw = {.data = &fBase.fReportDecriptor[0], .length = fBase.fDescriptor.wReportDescLength}}});
	fAttrs.Add({HID_DEVICE_MAX_INPUT_SIZE,  B_UINT16_TYPE, {.ui16 = fBase.fDescriptor.wMaxInputLength}});
	fAttrs.Add({HID_DEVICE_MAX_OUTPUT_SIZE, B_UINT16_TYPE, {.ui16 = fBase.fDescriptor.wMaxOutputLength}});
	fAttrs.Add({HID_DEVICE_VENDOR,          B_UINT16_TYPE, {.ui16 = fBase.fDescriptor.wVendorID}});
	fAttrs.Add({HID_DEVICE_PRODUCT,         B_UINT16_TYPE, {.ui16 = fBase.fDescriptor.wProductID}});
	fAttrs.Add({HID_DEVICE_VERSION,         B_UINT16_TYPE, {.ui16 = fBase.fDescriptor.wVersionID}});

	fAttrs.Add({});

	return B_OK;
}


const device_attr*
I2cHidDriver::HidDeviceImpl::Attributes() const
{
	return &fAttrs[0];
}

void*
I2cHidDriver::HidDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, HidDevice::ifaceName) == 0)
		return static_cast<HidDevice*>(this);

	return NULL;
}


// #pragma mark - HidDevice

void
I2cHidDriver::HidDeviceImpl::SetCallback(HidDeviceCallback* callback)
{
	fCallback = callback;
}


status_t
I2cHidDriver::HidDeviceImpl::Reset()
{
	uint8 cmd[] = {
		(uint8)(fBase.fDescriptor.wCommandRegister & 0xff),
		(uint8)(fBase.fDescriptor.wCommandRegister >> 8),
		0,
		I2C_HID_CMD_RESET,
	};

	return fBase.ExecCommand(I2C_OP_WRITE_STOP, cmd, sizeof(cmd), NULL, 0);
}


status_t
I2cHidDriver::HidDeviceImpl::Read(uint32 size, uint8* data)
{
	struct {
			uint16 reg;
	} _PACKED cmd = {
		fBase.fDescriptor.wInputRegister,
	};

	ScopeExit scopeExit([this]() {
		end_of_interrupt(fBase.fIrqVector);
	});

	return fBase.ExecCommand(I2C_OP_READ_STOP, (uint8*)&cmd, sizeof(cmd), data, size);
}


status_t
I2cHidDriver::HidDeviceImpl::Write(uint32 size, const uint8* data)
{
	CHECK_RET(fBase.fI2cBus->AcquireBus());
	ScopeExit busReleaser([this]() {
		fBase.fI2cBus->ReleaseBus();
	});

	struct {
			uint16 reg;
	} _PACKED cmd = {
		fBase.fDescriptor.wOutputRegister,
	};
	CHECK_RET(fBase.fI2cBus->ExecCommand(I2C_OP_WRITE, fBase.fDeviceAddress, (uint8*)&cmd, sizeof(cmd), NULL, 0));
	CHECK_RET(fBase.fI2cBus->ExecCommand(I2C_OP_WRITE_STOP, fBase.fDeviceAddress, data, size, NULL, 0));

	return B_OK;
}


status_t
I2cHidDriver::HidDeviceImpl::GetReport(uint8 reportType, uint8 reportId, uint32 size, uint8 *data)
{
	struct {
			uint16 reg1;
			uint8 value;
			uint8 command;

			uint16 reg2;
	} _PACKED cmd = {
		fBase.fDescriptor.wCommandRegister,
		(uint8)((reportId % 16) + ((reportType % 4) << 4)),
		I2C_HID_CMD_GET_REPORT,

		fBase.fDescriptor.wDataRegister
	};

	return fBase.ExecCommand(I2C_OP_READ_STOP, (uint8*)&cmd, sizeof(cmd), data, size);
}


status_t
I2cHidDriver::HidDeviceImpl::SetReport(uint8 reportType, uint8 reportId, uint32 size, const uint8* data)
{
	CHECK_RET(fBase.fI2cBus->AcquireBus());
	ScopeExit busReleaser([this]() {
		fBase.fI2cBus->ReleaseBus();
	});

	struct {
			uint16 reg;
			uint8 value;
			uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wCommandRegister,
		(uint8)((reportId % 16) + ((reportType % 4) << 4)),
		I2C_HID_CMD_SET_REPORT
	};
	CHECK_RET(fBase.fI2cBus->ExecCommand(I2C_OP_WRITE, fBase.fDeviceAddress, (uint8*)&cmd, sizeof(cmd), NULL, 0));

	struct {
			uint16 reg;
	} _PACKED cmd2 = {
		fBase.fDescriptor.wDataRegister,
	};
	CHECK_RET(fBase.fI2cBus->ExecCommand(I2C_OP_WRITE, fBase.fDeviceAddress, (uint8*)&cmd2, sizeof(cmd2), NULL, 0));
	CHECK_RET(fBase.fI2cBus->ExecCommand(I2C_OP_WRITE_STOP, fBase.fDeviceAddress, data, size, NULL, 0));

	return B_OK;
}


status_t
I2cHidDriver::HidDeviceImpl::GetIdle(uint8 reportId, uint16* idle)
{
	struct {
			uint16 reg;
			uint8 value;
			uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wCommandRegister,
		(uint8)(reportId % 16),
		I2C_HID_CMD_GET_IDLE
	};

	struct {
		uint16 size;
		uint16 value;
	} _PACKED reply;

	CHECK_RET(fBase.ExecCommand(I2C_OP_READ_STOP, (uint8*)&cmd, sizeof(cmd), (uint8*)&reply, sizeof(reply)));

	if (reply.size != 4)
		return B_BAD_VALUE;

	*idle = reply.value;
	return B_OK;
}


status_t
I2cHidDriver::HidDeviceImpl::SetIdle(uint8 reportId, uint16 idle)
{
	struct {
			uint16 reg1;
			uint16 size1;
			uint16 value1;

			uint16 reg2;
			uint8 value2;
			uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wDataRegister,
		4,
		idle,

		fBase.fDescriptor.wCommandRegister,
		(uint8)(reportId % 16),
		I2C_HID_CMD_SET_IDLE
	};

	return fBase.ExecCommand(I2C_OP_WRITE_STOP, (uint8*)&cmd, sizeof(cmd), NULL, 0);
}


status_t
I2cHidDriver::HidDeviceImpl::GetProtocol(uint16* protocol)
{
	struct {
			uint16 reg;
			uint8 value;
			uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wCommandRegister,
		0,
		I2C_HID_CMD_GET_PROTOCOL
	};

	struct {
		uint16 size;
		uint16 value;
	} _PACKED reply;

	CHECK_RET(fBase.ExecCommand(I2C_OP_READ_STOP, (uint8*)&cmd, sizeof(cmd), (uint8*)&reply, sizeof(reply)));

	if (reply.size != 4)
		return B_BAD_VALUE;

	*protocol = reply.value;
	return B_OK;
}


status_t
I2cHidDriver::HidDeviceImpl::SetProtocol(uint16 protocol)
{
	struct {
			uint16 reg1;
			uint16 size1;
			uint16 value1;

			uint16 reg2;
			uint8 value2;
			uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wDataRegister,
		4,
		protocol,

		fBase.fDescriptor.wCommandRegister,
		0,
		I2C_HID_CMD_SET_PROTOCOL
	};

	return fBase.ExecCommand(I2C_OP_WRITE_STOP, (uint8*)&cmd, sizeof(cmd), NULL, 0);
}


status_t
I2cHidDriver::HidDeviceImpl::SetPower(uint8 power)
{
	struct {
			uint16 reg;
			uint8 power;
			uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wCommandRegister,
		power,
		I2C_HID_CMD_SET_POWER
	};

	return fBase.ExecCommand(I2C_OP_WRITE_STOP, (uint8*)&cmd, sizeof(cmd), NULL, 0);
}


static driver_module_info sI2cHidDriverModule = {
	.info = {
		.name = I2C_HID_DRIVER_MODULE_NAME,
	},
	.probe = I2cHidDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sI2cHidDriverModule,
	NULL
};
