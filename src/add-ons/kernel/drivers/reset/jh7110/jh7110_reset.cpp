#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Reset.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define JH7110_RESET_DRIVER_MODULE_NAME "drivers/reset/jh7110_reset/driver/v1"


#define AONCRG_RESET_ASSERT		0x038
#define AONCRG_RESET_STATUS		0x03C
#define ISPCRG_RESET_ASSERT		0x038
#define ISPCRG_RESET_STATUS		0x03C
#define VOUTCRG_RESET_ASSERT	0x048
#define VOUTCRG_RESET_STATUS	0x04C
#define STGCRG_RESET_ASSERT		0x074
#define STGCRG_RESET_STATUS		0x078
#define SYSCRG_RESET_ASSERT0	0x2F8
#define SYSCRG_RESET_ASSERT1	0x2FC
#define SYSCRG_RESET_ASSERT2	0x300
#define SYSCRG_RESET_ASSERT3	0x304
#define SYSCRG_RESET_STATUS0	0x308
#define SYSCRG_RESET_STATUS1	0x30C
#define SYSCRG_RESET_STATUS2	0x310
#define SYSCRG_RESET_STATUS3	0x314

enum JH7110_RESET_CRG_GROUP {
	SYSCRG_0 = 0,
	SYSCRG_1,
	SYSCRG_2,
	SYSCRG_3,
	STGCRG,
	AONCRG,
	ISPCRG,
	VOUTCRG,
};


class Jh7110ResetDriver: public DeviceDriver, public ResetController {
private:
	struct MmioRange {
		AreaDeleter area;
		size_t size {};
		uint32 volatile* regs {};

		status_t Init(phys_addr_t physAdr, size_t size);
	};

	struct AssertAndStatus {
		uint32 volatile* assert;
		uint32 volatile* status;
	};

	class Jh7110ResetDevice: public ResetDevice {
	public:
		Jh7110ResetDevice(Jh7110ResetDriver& base): fBase(base) {}

		int32 Id() const {return this - fBase.GetReset(0);};

		DeviceNode* OwnerNode() final;

		virtual bool IsAsserted() const final;
		virtual status_t SetAsserted(bool doAssert) final;

	private:
		Jh7110ResetDriver& fBase;
	};

public:
	Jh7110ResetDriver(DeviceNode* node);
	virtual ~Jh7110ResetDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// ResetController
	ResetDevice* GetDevice(const uint8* optInfo, uint32 optInfoSize) final;

private:
	status_t Init();
	bool GetAssertAndStatus(uint32 id, AssertAndStatus& res);
	Jh7110ResetDevice* GetReset(int32 id) {return (Jh7110ResetDevice*)fResets[id];}

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	MmioRange fSyscrg;
	MmioRange fStgcrg;
	MmioRange fAoncrg;
	MmioRange fIspcrg;
	MmioRange fVoutcrg;

	char fResets[32*8][sizeof(Jh7110ResetDevice)];
};


status_t
Jh7110ResetDriver::MmioRange::Init(phys_addr_t physAdr, size_t size)
{
	area.SetTo(map_physical_memory("StarfiveReset MMIO", physAdr, size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&regs));

	CHECK_RET(area.Get());

	return B_OK;
}


// #pragma mark - Jh7110ResetDriver

status_t
Jh7110ResetDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<Jh7110ResetDriver> driver(new(std::nothrow) Jh7110ResetDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


Jh7110ResetDriver::Jh7110ResetDriver(DeviceNode* node): fNode(node)
{
	for (auto clock: fResets)
		new(clock) Jh7110ResetDevice(*this);
}


Jh7110ResetDriver::~Jh7110ResetDriver()
{
	for (auto clock: fResets)
		((Jh7110ResetDevice*)clock)->~Jh7110ResetDevice();
}


status_t
Jh7110ResetDriver::Init()
{
	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	uint64 regsLen = 0;
	CHECK_RET(fFdtDevice->GetRegByName("syscrg", &regs, &regsLen));
	CHECK_RET(fSyscrg.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetRegByName("stgcrg", &regs, &regsLen));
	CHECK_RET(fStgcrg.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetRegByName("aoncrg", &regs, &regsLen));
	CHECK_RET(fAoncrg.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetRegByName("ispcrg", &regs, &regsLen));
	CHECK_RET(fIspcrg.Init(regs, regsLen));

	CHECK_RET(fFdtDevice->GetRegByName("voutcrg", &regs, &regsLen));
	CHECK_RET(fVoutcrg.Init(regs, regsLen));

	return B_OK;
}


bool
Jh7110ResetDriver::GetAssertAndStatus(uint32 id, AssertAndStatus& res)
{
	uint32 group = id / 32;
	switch (group) {
		case SYSCRG_0:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT0 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS0 / 4
			};
			return true;
		case SYSCRG_1:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT1 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS1 / 4
			};
			return true;
		case SYSCRG_2:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT2 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS2 / 4
			};
			return true;
		case SYSCRG_3:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT3 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS3 / 4
			};
			return true;
		case STGCRG:
			res = AssertAndStatus{
				fStgcrg.regs + STGCRG_RESET_ASSERT / 4,
				fStgcrg.regs + STGCRG_RESET_STATUS / 4
			};
			return true;
		case AONCRG:
			res = AssertAndStatus{
				fAoncrg.regs + AONCRG_RESET_ASSERT / 4,
				fAoncrg.regs + AONCRG_RESET_STATUS / 4
			};
			return true;
		case ISPCRG:
			res = AssertAndStatus{
				fIspcrg.regs + ISPCRG_RESET_ASSERT / 4,
				fIspcrg.regs + ISPCRG_RESET_STATUS / 4
			};
			return true;
		case VOUTCRG:
			res = AssertAndStatus{
				fVoutcrg.regs + VOUTCRG_RESET_ASSERT / 4,
				fVoutcrg.regs + VOUTCRG_RESET_STATUS / 4
			};
			return true;
	}
	return false;
}


void*
Jh7110ResetDriver::QueryInterface(const char* name)
{
	if (strcmp(name, ResetController::ifaceName) == 0)
		return static_cast<ResetController*>(this);

	return NULL;
}


ResetDevice*
Jh7110ResetDriver::GetDevice(const uint8* optInfo, uint32 optInfoSize)
{
	if (optInfoSize != 4)
		return NULL;

	uint32 id = B_BENDIAN_TO_HOST_INT32(*(const uint32*)optInfo);
	if (id >= B_COUNT_OF(fResets))
		return NULL;

	return GetReset(id);
}


// #pragma mark - Jh7110ResetDevice

DeviceNode*
Jh7110ResetDriver::Jh7110ResetDevice::OwnerNode()
{
	fBase.fNode->AcquireReference();
	return fBase.fNode;
}


bool
Jh7110ResetDriver::Jh7110ResetDevice::IsAsserted() const
{
	int32 id = Id();

	AssertAndStatus assertAndStatus {};
	if (!fBase.GetAssertAndStatus(id, assertAndStatus))
		return false;

	uint32 mask = 1 << (id % 32);
	uint32 value = *assertAndStatus.status;

	return (value & mask) == 0;
}


status_t
Jh7110ResetDriver::Jh7110ResetDevice::SetAsserted(bool doAssert)
{
	int32 id = Id();

	AssertAndStatus assertAndStatus {};
	if (!fBase.GetAssertAndStatus(id, assertAndStatus))
		return EINVAL;

	uint32 mask = 1 << (id % 32);
	uint32 done = 0;

	uint32 value = *assertAndStatus.assert;
	if (doAssert) {
		value |= mask;
	} else {
		value &= ~mask;
		done ^= mask;
	}

	*assertAndStatus.assert = value;

	uint32 attempts = 10000;
	do {
		value = *assertAndStatus.status;
	} while((value & mask) != done && --attempts != 0);

	return B_OK;
}


static driver_module_info sJh7110ResetDriverModule = {
	.info = {
		.name = JH7110_RESET_DRIVER_MODULE_NAME,
	},
	.probe = Jh7110ResetDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sJh7110ResetDriverModule,
	NULL
};
