#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/bus/PS2.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <condition_variable.h>
#include <util/DoublyLinkedList.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define ALTERA_PS2_DRIVER_MODULE_NAME "busses/ps2/altera_ps2/driver/v1"


enum {
	ps2CmdReset                = 0xff,
	ps2CmdResend               = 0xfe,
	ps2CmdSetDefaults          = 0xf6,
	ps2CmdDisableDataReporting = 0xf5,
	ps2CmdEnableDataReporting  = 0xf4,
	ps2CmdSetSampleRate        = 0xf3,
	ps2CmdGetDevId             = 0xf2,

	ps2ReplyAck = 0xfa,
};

enum {
	ps2DevIdMouseGeneric = 0x0000,
	ps2DevIdMouseWheel   = 0x0003,
	ps2DevIdKeyboard     = 0x83AB,
};


struct AlteraPs2Regs {
	union Data {
		struct {
			uint32 data:     8;
			uint32 unknown1: 7;
			uint32 isAvail:  1;
			uint32 avail:   16;
		};
		uint32 val;
	} data;
	union Control {
		struct {
			uint32 irqEnabled: 1;
			uint32 unknown1:   7;
			uint32 irqPending: 1;
			uint32 unknown2:   1;
			uint32 error:      1;
			uint32 unknown3:  21;
		};
		uint32 val;
	} control;
};


class AlteraPs2Driver: public DeviceDriver {
public:
	AlteraPs2Driver(DeviceNode* node): fNode(node), fCallback(*this), fPs2Device(*this) {}
	virtual ~AlteraPs2Driver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();
	status_t Read(uint8& val, uint32 flags = 0, bigtime_t timeout = 0);

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	AlteraPs2Regs volatile* fRegs {};
	uint64 fRegsLen {};

	long fIrqVector = -1;

	ConditionVariable fCanReadCond;

	struct IoRequest {
		IoRequest(uint8* data, uint32 size): dataBeg(data), dataEnd(data + size), data(data)
		{
			completedCond.Init(this, "IoRequest");
		}

		DoublyLinkedListLink<IoRequest> link;

		typedef DoublyLinkedList<
			IoRequest, DoublyLinkedListMemberGetLink<IoRequest, &IoRequest::link>
		> List;

		ConditionVariable completedCond;

		uint8* dataBeg;
		uint8* dataEnd;
		uint8* data;
	};

	IoRequest::List fIoRequests;


	class Callback: public Ps2DeviceCallback {
	public:
		Callback(AlteraPs2Driver& base): fBase(base) {}
		void InputAvailable() final;

	private:
		AlteraPs2Driver& fBase;
	} fCallback;

	class Ps2DeviceImpl: public BusDriver, public Ps2Device {
	public:
		Ps2DeviceImpl(AlteraPs2Driver& base): fBase(base) {}

		// BusDriver
		void* QueryInterface(const char* name) final;

		// Ps2Device
		status_t SetCallback(Ps2DeviceCallback* callback) final;
		status_t Read(uint8* data) final;
		status_t Write(uint8 data) final;

	public:
		AlteraPs2Driver& fBase;
		Ps2DeviceCallback* fCallback {};
	} fPs2Device;
};


AlteraPs2Driver::~AlteraPs2Driver()
{
	if (fRegs != NULL)
		fRegs->control.irqEnabled = false;

	if (fIrqVector >= 0)
		remove_io_interrupt_handler(fIrqVector, HandleInterrupt, this);
}


status_t
AlteraPs2Driver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<AlteraPs2Driver> driver(new(std::nothrow) AlteraPs2Driver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
AlteraPs2Driver::Init()
{
	dprintf("AlteraPs2Driver::Init()\n");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	if (!fFdtDevice->GetReg(0, &regs, &fRegsLen))
		return B_ERROR;

	CHECK_RET(fFdtDevice->GetInterruptVector(0, &fIrqVector));

	install_io_interrupt_handler(fIrqVector, HandleInterrupt, this, 0);

	fRegsArea.SetTo(map_physical_memory("Altera PS/2 MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	fRegs->control.irqEnabled = true;

	fCanReadCond.Init(this, "canRead");

	fPs2Device.SetCallback(&fCallback);

	uint8 val8 {};
	fPs2Device.Write(ps2CmdDisableDataReporting);

	CHECK_RET(Read(val8));
	while (val8 != ps2ReplyAck) {
		CHECK_RET(Read(val8));
	}

	fPs2Device.Write(ps2CmdGetDevId);
	CHECK_RET(Read(val8));
	while (val8 != ps2ReplyAck) {
		CHECK_RET(Read(val8));
	}

	uint32 devId = 0;
	uint32 devIdLen = 0;

	bigtime_t timeout = system_time() + 100000;
	while (Read(val8, B_ABSOLUTE_TIMEOUT, timeout) >= B_OK) {
			devId = (devId << 8) + val8;
			devIdLen++;
	}
	dprintf("  devId: %#" B_PRIx32 "\n", devId);
	dprintf("  devIdLen: %" B_PRIu32 "\n", devIdLen);

	fPs2Device.SetCallback(NULL);

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "PS/2 Device"}},
		{B_DEVICE_BUS,         B_STRING_TYPE, {.string = "ps2"}},

		{PS2_DEVICE_ID, B_UINT32_TYPE, {.ui32 = 0x83AB02 /* !!! */}},

		{}
	};

	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fPs2Device), attrs, NULL));

	return B_OK;
}


int32
AlteraPs2Driver::HandleInterrupt(void* arg)
{
	return static_cast<AlteraPs2Driver*>(arg)->HandleInterruptInt();
}


int32
AlteraPs2Driver::HandleInterruptInt()
{
	dprintf("AlteraPs2Driver::HandleInterruptInt()\n");

	auto Consume = [&](uint8 val) {
		IoRequest* req = fIoRequests.First();
		*req->data++ = val;
		if (req->data == req->dataEnd) {
			fIoRequests.Remove(req);
			req->completedCond.NotifyAll(B_OK);
		}
	};

	uint8 val;
	while (!fIoRequests.IsEmpty() && fPs2Device.Read(&val) >= 0) {
		Consume(val);
	}

	if (fPs2Device.fCallback != NULL)
		fPs2Device.fCallback->InputAvailable();

	return B_HANDLED_INTERRUPT;
}


status_t
AlteraPs2Driver::Read(uint8& val, uint32 flags, bigtime_t timeout)
{
	for (;;) {
		status_t len = fPs2Device.Read(&val);
		CHECK_RET(len);
		if (len == 0) {
			CHECK_RET(fCanReadCond.Wait(flags, timeout));
			continue;
		}
		return B_OK;
	}
}


void
AlteraPs2Driver::Callback::InputAvailable()
{
	dprintf("AlteraPs2Driver::Callback::InputAvailable()\n");
	fBase.fCanReadCond.NotifyOne();
}


void*
AlteraPs2Driver::Ps2DeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, Ps2Device::ifaceName) == 0)
		return static_cast<Ps2Device*>(this);

	return NULL;
}


status_t
AlteraPs2Driver::Ps2DeviceImpl::SetCallback(Ps2DeviceCallback* callback)
{
	return ENOSYS;
}


status_t
AlteraPs2Driver::Ps2DeviceImpl::Read(uint8* val)
{
	AlteraPs2Regs::Data data {.val = fBase.fRegs->data.val};
	*val = data.data;
	return data.avail;
}


status_t
AlteraPs2Driver::Ps2DeviceImpl::Write(uint8 val)
{
	fBase.fRegs->data.val = AlteraPs2Regs::Data{.data = val}.val;
	AlteraPs2Regs::Control control{.val = fBase.fRegs->control.val};
	if (control.error) {
		control.error = false;
		fBase.fRegs->control.val = control.val;
		return B_ERROR;
	}
	return B_OK;
}


static driver_module_info sAlteraPs2DriverModule = {
	.info = {
		.name = ALTERA_PS2_DRIVER_MODULE_NAME,
	},
	.probe = AlteraPs2Driver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sAlteraPs2DriverModule,
	NULL
};
