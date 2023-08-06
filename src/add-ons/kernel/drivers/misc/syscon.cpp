#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Syscon.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define SYSCON_DRIVER_MODULE_NAME "drivers/misc/syscon/driver/v1"


class SysconDriver: public DeviceDriver, public SysconDevice {
public:
	SysconDriver(DeviceNode* node): fNode(node) {}
	virtual ~SysconDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// SysconDevice
	status_t Read4(uint32 offset, uint32 mask, uint32* value) final;
	status_t Write4(uint32 offset, uint32 mask, uint32 value) final;

private:
	status_t Init();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	uint32 volatile* fRegs {};
	uint64 fRegsLen {};
};


SysconDriver::~SysconDriver()
{
}


status_t
SysconDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<SysconDriver> driver(new(std::nothrow) SysconDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
SysconDriver::Init()
{
	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	if (!fFdtDevice->GetReg(0, &regs, &fRegsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("Syscon MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	return B_OK;
}


void*
SysconDriver::QueryInterface(const char* name)
{
	if (strcmp(name, SysconDevice::ifaceName) == 0)
		return static_cast<SysconDevice*>(this);

	return NULL;
}


status_t
SysconDriver::Read4(uint32 offset, uint32 mask, uint32* outValue)
{
	if (offset % 4 != 0)
		return B_BAD_VALUE;

	if (fRegsLen < 4 || offset >= fRegsLen - 4)
		return B_BAD_INDEX;

	uint32 value = fRegs[offset / 4];
	value &= mask;
	*outValue = value;

	return B_OK;
}


status_t
SysconDriver::Write4(uint32 offset, uint32 mask, uint32 value)
{
	if (offset % 4 != 0)
		return B_BAD_VALUE;

	if (fRegsLen < 4 || offset >= fRegsLen - 4)
		return B_BAD_INDEX;

	uint32 oldValue = fRegs[offset / 4];
	value = (oldValue & ~mask) | (value & mask);
	fRegs[offset / 4] = value;

	return B_OK;
}


static driver_module_info sSysconDriverModule = {
	.info = {
		.name = SYSCON_DRIVER_MODULE_NAME,
	},
	.probe = SysconDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sSysconDriverModule,
	NULL
};
