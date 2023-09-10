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
#include <util/AutoLock.h>
#include <util/Vector.h>
#include <DPC.h>

#include "I2CHIDProtocol.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define I2C_HID_DRIVER_MODULE_NAME "busses/hid/i2c_hid/driver/v1"


class I2cHidDriver: public DeviceDriver, private DPCCallback {
public:
	I2cHidDriver(DeviceNode* node): fNode(node), fHidDevice(*this) {}
	virtual ~I2cHidDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	status_t ExecCommand(const i2c_chunk* chunks, uint32 chunkCount);
	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

	// DPCCallback
	void DoDPC(DPCQueue* queue) final;

private:
	mutex fLock = MUTEX_INITIALIZER("i2c_hid");

	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};
	I2cBus* fI2cBus {};

	uint32 fDeviceAddress {};
	uint16 fDescriptorAddress {};
	long fIrqVector = -1;

	i2c_hid_descriptor fDescriptor {};

	uint8* fInputBuffer {};
	uint32 fInputBufferSize {};
	HidInputCallback* fInputCallback {};

	class HidDeviceImpl: public BusDriver, public HidDevice {
	public:
		HidDeviceImpl(I2cHidDriver& base): fBase(base) {}

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
		I2cHidDriver& fBase;
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
	fI2cBus = i2cBusNode->QueryDriverInterface<I2cBus>(fNode);

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

	dprintf("  fDeviceAddress: %" B_PRIu32 "\n", fDeviceAddress);
	dprintf("  fDescriptorAddress: %" B_PRIu32 "\n", fDescriptorAddress);

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&fDescriptorAddress, .length = sizeof(fDescriptorAddress), .isWrite = true},
		{.buffer = (uint8*)&fDescriptor,        .length = sizeof(fDescriptor),        .isWrite = false},
	};

	CHECK_RET(ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks)));
	dprintf("  fDescriptor.wHIDDescLength: %" B_PRIu16 "\n", fDescriptor.wHIDDescLength);
	dprintf("  fDescriptor.wReportDescLength: %" B_PRIu16 "\n", fDescriptor.wReportDescLength);
	dprintf("  fDescriptor.wMaxInputLength: %" B_PRIu16 "\n", fDescriptor.wMaxInputLength);
	dprintf("  fDescriptor.wMaxOutputLength: %" B_PRIu16 "\n", fDescriptor.wMaxOutputLength);

	ArrayDeleter<uint8> reportDecriptor(new(std::nothrow) uint8[fDescriptor.wReportDescLength]);
	if (!reportDecriptor.IsSet())
		return B_NO_MEMORY;

	i2c_chunk i2cChunks2[] = {
		{.buffer = (uint8*)&fDescriptor.wReportDescRegister, .length = sizeof(fDescriptor.wReportDescRegister), .isWrite = true},
		{.buffer =         &reportDecriptor[0],              .length = fDescriptor.wReportDescLength,           .isWrite = false},
	};

	CHECK_RET(ExecCommand(i2cChunks2, B_COUNT_OF(i2cChunks2)));

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "HID Device"}},
		{B_DEVICE_BUS,         B_STRING_TYPE, {.string = "hid"}},

		{HID_DEVICE_REPORT_DESC,     B_RAW_TYPE,    {.raw = {.data = &reportDecriptor[0], .length = fDescriptor.wReportDescLength}}},
		{HID_DEVICE_MAX_INPUT_SIZE,  B_UINT16_TYPE, {.ui16 = fDescriptor.wMaxInputLength}},
		{HID_DEVICE_MAX_OUTPUT_SIZE, B_UINT16_TYPE, {.ui16 = fDescriptor.wMaxOutputLength}},
		{HID_DEVICE_VENDOR,          B_UINT16_TYPE, {.ui16 = fDescriptor.wVendorID}},
		{HID_DEVICE_PRODUCT,         B_UINT16_TYPE, {.ui16 = fDescriptor.wProductID}},
		{HID_DEVICE_VERSION,         B_UINT16_TYPE, {.ui16 = fDescriptor.wVersionID}},

		{}
	};

	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fHidDevice), attrs, NULL));

	return B_OK;
}


status_t
I2cHidDriver::ExecCommand(const i2c_chunk* chunks, uint32 chunkCount)
{
	CHECK_RET(fI2cBus->AcquireBus());
	ScopeExit busReleaser([this]() {
		fI2cBus->ReleaseBus();
	});

	CHECK_RET(fI2cBus->ExecCommand(fDeviceAddress, chunks, chunkCount));

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
	DPCQueue::DefaultQueue(B_URGENT_DISPLAY_PRIORITY)->Add(this);
	return B_HANDLED_INTERRUPT;
}


void
I2cHidDriver::DoDPC(DPCQueue* queue)
{
	remove_io_interrupt_handler(fIrqVector, HandleInterrupt, this);
	end_of_interrupt(fIrqVector);

	MutexLocker lock(&fLock);

	// canceled
	if (fInputBuffer == NULL)
		return;

	struct {
		uint16 reg;
	} _PACKED cmd = {
		fDescriptor.wInputRegister,
	};

	uint16 actualSize;

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd,        .length = sizeof(cmd),        .isWrite = true},
		{.buffer = (uint8*)&actualSize, .length = sizeof(actualSize), .isWrite = false},
		{.buffer = fInputBuffer,        .length = fInputBufferSize,   .isWrite = false},
	};

	status_t res = ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));

	// handle reset
	if (res >= B_OK && actualSize == 0) {
		install_io_interrupt_handler(fIrqVector, HandleInterrupt, this, B_DEFERRED_COMPLETION);
		return;
	}
	actualSize = (actualSize) < 2 ? 0 : actualSize - 2;

#if 0
	dprintf("I2cHidDriver::InputCallback(%#" B_PRIx32 ", %" B_PRIu32 ")\n", res, actualSize);

	for (size_t i = 0; i < actualSize; i++) {
		dprintf(" %02x", ((uint8*)fInputBuffer)[i]);
		if (i == 15)
			dprintf("\n");
	}
	if (actualSize % 16 != 0)
		dprintf("\n");
#endif

	uint8* buffer = fInputBuffer;
	HidInputCallback* callback = fInputCallback;
	fInputBuffer = NULL;
	lock.Unlock();

	callback->InputAvailable(res, buffer, res < B_OK ? 0 : actualSize);
}


// #pragma mark - BusDriver

void*
I2cHidDriver::HidDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, HidDevice::ifaceName) == 0)
		return static_cast<HidDevice*>(this);

	return NULL;
}


void
I2cHidDriver::HidDeviceImpl::DriverAttached(bool isAttached)
{
	if (!isAttached) {
		MutexLocker lock(&fBase.fLock);
		if (fBase.fInputBuffer != NULL) {
			uint8* buffer = fBase.fInputBuffer;
			HidInputCallback* callback = fBase.fInputCallback;
			fBase.fInputBuffer = NULL;
			lock.Unlock();

			callback->InputAvailable(B_CANCELED, buffer, 0);
		}
	}
}


// #pragma mark - HidDevice

status_t
I2cHidDriver::HidDeviceImpl::Reset()
{
	struct {
		uint16 reg;
		uint8 value;
		uint8 command;
	} _PACKED cmd = {
		fBase.fDescriptor.wCommandRegister,
		0,
		I2C_HID_CMD_RESET,
	};

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
	};

	return fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));
}


status_t
I2cHidDriver::HidDeviceImpl::RequestRead(uint32 size, uint8* data, HidInputCallback* callback)
{
	if (data == NULL || callback == NULL)
		return B_BAD_VALUE;

	MutexLocker lock(&fBase.fLock);

	if (fBase.fInputBuffer != NULL)
		return B_BUSY;

	fBase.fInputBuffer = data;
	fBase.fInputBufferSize = size;
	fBase.fInputCallback = callback;

	install_io_interrupt_handler(fBase.fIrqVector, HandleInterrupt, &fBase, B_DEFERRED_COMPLETION);

	return B_OK;
}


status_t
I2cHidDriver::HidDeviceImpl::Write(uint32 size, const uint8* data)
{
	struct {
		uint16 reg;
	} _PACKED cmd = {
		fBase.fDescriptor.wOutputRegister,
	};

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
		{.buffer = (uint8*)data, .length = size,        .isWrite = true},
	};

	return fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));
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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
		{.buffer =         data, .length = size,        .isWrite = false},
	};

	return fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));
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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
	};

	CHECK_RET(fBase.fI2cBus->ExecCommand(fBase.fDeviceAddress, i2cChunks, B_COUNT_OF(i2cChunks)));


	struct {
		uint16 reg;
	} _PACKED cmd2 = {
		fBase.fDescriptor.wDataRegister,
	};

	i2c_chunk i2cChunks2[] = {
		{.buffer = (uint8*)&cmd2, .length = sizeof(cmd2), .isWrite = true},
		{.buffer = (uint8*)&data, .length = size, .isWrite = true},
	};

	CHECK_RET(fBase.fI2cBus->ExecCommand(fBase.fDeviceAddress, i2cChunks2, B_COUNT_OF(i2cChunks2)));

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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd,   .length = sizeof(cmd),   .isWrite = true},
		{.buffer = (uint8*)&reply, .length = sizeof(reply), .isWrite = false},
	};

	CHECK_RET(fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks)));

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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
	};

	return fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));
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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd,   .length = sizeof(cmd),   .isWrite = true},
		{.buffer = (uint8*)&reply, .length = sizeof(reply), .isWrite = false},
	};

	CHECK_RET(fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks)));

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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
	};

	return fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));
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

	i2c_chunk i2cChunks[] = {
		{.buffer = (uint8*)&cmd, .length = sizeof(cmd), .isWrite = true},
	};

	return fBase.ExecCommand(i2cChunks, B_COUNT_OF(i2cChunks));
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
