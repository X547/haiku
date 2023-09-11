#include <assert.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#include <arch/generic/cache_controller.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define FU740_CACHE_DRIVER_MODULE_NAME "drivers/misc/fu740_cache/driver/v1"


struct L2CacheRegs {
	uint32 unknown1[128];
	uint64 flush64;
};

static_assert(offsetof(L2CacheRegs, flush64) == 0x200);


class Fu740CacheDriver: public DeviceDriver, public CacheController {
public:
	Fu740CacheDriver(DeviceNode* node): fNode(node) {}
	virtual ~Fu740CacheDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	// CacheController
	uint32 CacheBlockSize() final {return fCacheBlockSize;}
	void FlushCache(phys_addr_t adr) final;

private:
	status_t Init();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};
	bool fIsInstalled = false;

	uint32 fCacheBlockSize {};

	AreaDeleter fRegsArea;
	L2CacheRegs volatile* fRegs {};
	uint64 fRegsLen {};
};


status_t
Fu740CacheDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<Fu740CacheDriver> driver(new(std::nothrow) Fu740CacheDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


Fu740CacheDriver::~Fu740CacheDriver()
{
	if (fIsInstalled)
		uninstall_cache_controller(static_cast<CacheController*>(this));
}


status_t
Fu740CacheDriver::Init()
{
	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	CHECK_RET(fFdtDevice->GetPropUint32("cache-block-size", fCacheBlockSize));

	uint64 regs = 0;
	CHECK_RET(fFdtDevice->GetRegByName("control", &regs, &fRegsLen));

	fRegsArea.SetTo(map_physical_memory("Fu740Cache MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	CHECK_RET(install_cache_controller(static_cast<CacheController*>(this)));
	fIsInstalled = true;

	return B_OK;
}


void
Fu740CacheDriver::FlushCache(phys_addr_t adr)
{
	fRegs->flush64 = adr;
}


static driver_module_info sFu740CacheDriverModule = {
	.info = {
		.name = FU740_CACHE_DRIVER_MODULE_NAME,
	},
	.probe = Fu740CacheDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sFu740CacheDriverModule,
	NULL
};
