#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Clock.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define DWMAC_DRIVER_MODULE_NAME "drivers/network/dwmac/driver/v1"


class DwmacDriver: public DeviceDriver {
public:
	DwmacDriver(DeviceNode* node): fNode(node) {}
	virtual ~DwmacDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	uint32 volatile* fRegs {};
	uint64 fRegsLen {};

	ClockDevice* fTxClock {};
	ClockDevice* fRmiiRtxClock {};
};


status_t
DwmacDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<DwmacDriver> driver(new(std::nothrow) DwmacDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
DwmacDriver::Init()
{
	dprintf("DwmacDriver::Init()\n");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	if (!fFdtDevice->GetReg(0, &regs, &fRegsLen))
		return B_ERROR;

	dprintf("  regs: %#" B_PRIx64 "\n", regs);

	fRegsArea.SetTo(map_physical_memory("DWMAC MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	CHECK_RET(fFdtDevice->GetClockByName("gtx", &fTxClock));
	CHECK_RET(fFdtDevice->GetClockByName("rmii_rtx", &fRmiiRtxClock));

	dprintf("  gtx\n");
	dprintf("    enabled: %d\n", fTxClock->IsEnabled());
	dprintf("    rate: %" B_PRId64 " Hz\n", fTxClock->GetRate());
	dprintf("  rmii_rtx\n");
	dprintf("    enabled: %d\n", fRmiiRtxClock->IsEnabled());
	dprintf("    rate: %" B_PRId64 " Hz\n", fRmiiRtxClock->GetRate());

	return B_OK;
}


static driver_module_info sDwmacDriverModule = {
	.info = {
		.name = DWMAC_DRIVER_MODULE_NAME,
	},
	.probe = DwmacDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sDwmacDriverModule,
	NULL
};
