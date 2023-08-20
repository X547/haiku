#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Syscon.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>

extern "C" {
	status_t set_shutdown_hook(status_t (*shutdown)(bool reboot));
}


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define SYSCON_POWEROFF_DRIVER_MODULE_NAME "drivers/power/power_syscon/poweroff/driver/v1"
#define SYSCON_REBOOT_DRIVER_MODULE_NAME   "drivers/power/power_syscon/reboot/driver/v1"


class PowerSysconDriver: public DeviceDriver {
public:
	PowerSysconDriver(DeviceNode* node, bool isReboot): fNode(node), fIsReboot(isReboot) {}
	virtual ~PowerSysconDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, bool isReboot, DeviceDriver** driver);
	static status_t ProbePoweroff(DeviceNode* node, DeviceDriver** driver);
	static status_t ProbeReboot(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	status_t Call();

	static status_t Shutdown(bool reboot);

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};
	DeviceNodePutter fSysconNode;
	SysconDevice* fSysconDevice {};

	uint32 fOffset {};
	uint32 fValue {};
	uint32 fMask = 0xffffffff;

	bool fIsReboot;
	bool fHookInstalled = false;
};


static int32 sDriverCount;
static PowerSysconDriver* sPoweroffDriver;
static PowerSysconDriver* sRebootDriver;


PowerSysconDriver::~PowerSysconDriver()
{
	int32 oldCount;
	if (fIsReboot && sRebootDriver == this) {
		sRebootDriver = NULL;
		oldCount = atomic_add(&sDriverCount, -1);
	}
	else if (!fIsReboot && sPoweroffDriver == this) {
		sPoweroffDriver = NULL;
		oldCount = atomic_add(&sDriverCount, -1);
	} else {
		return;
	}
	if (oldCount == 1)
		set_shutdown_hook(NULL);
}


status_t
PowerSysconDriver::Probe(DeviceNode* node, bool isReboot, DeviceDriver** outDriver)
{
	ObjectDeleter<PowerSysconDriver> driver(new(std::nothrow) PowerSysconDriver(node, isReboot));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
PowerSysconDriver::ProbePoweroff(DeviceNode* node, DeviceDriver** outDriver)
{
	return Probe(node, false, outDriver);
}


status_t
PowerSysconDriver::ProbeReboot(DeviceNode* node, DeviceDriver** outDriver)
{
	return Probe(node, true, outDriver);
}


status_t
PowerSysconDriver::Init()
{
	// dprintf("PowerSysconDriver::Init()\n");
	// dprintf("  fIsReboot: %d\n", fIsReboot);

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint32 regmapPhandle;
	CHECK_RET(fFdtDevice->GetPropUint32("regmap", regmapPhandle));
	// dprintf("  regmapPhandle: %" B_PRIu32 "\n", regmapPhandle);

	CHECK_RET(fFdtDevice->GetPropUint32("offset", fOffset));
	// dprintf("  fOffset: %" B_PRIu32 "\n", fOffset);

	CHECK_RET(fFdtDevice->GetPropUint32("value", fValue));
	// dprintf("  fValue: %" B_PRIu32 "\n", fValue);

	status_t res = fFdtDevice->GetPropUint32("mask", fMask);
	if (res < B_OK && res != B_NAME_NOT_FOUND)
		return res;

	DeviceNodePutter fdtBusNode(fFdtDevice->GetBus());
	FdtBus* fdtBus = fdtBusNode->QueryDriverInterface<FdtBus>();
	// dprintf("  fdtBus: %p\n", fdtBus);

	fSysconNode.SetTo(fdtBus->NodeByPhandle(regmapPhandle));
	// dprintf("  fSysconNode: %p\n", fSysconNode.Get());
	fSysconDevice = fSysconNode->QueryDriverInterface<SysconDevice>();
	// dprintf("  fSysconDevice: %p\n", fSysconDevice);

	int32 oldCount;
	if (fIsReboot && sRebootDriver == NULL) {
		sRebootDriver = this;
		oldCount = atomic_add(&sDriverCount, 1);
	} else if (!fIsReboot && sPoweroffDriver == NULL) {
		sPoweroffDriver = this;
		oldCount = atomic_add(&sDriverCount, 1);
	} else {
		return B_ERROR;
	}

	if (oldCount == 0)
		set_shutdown_hook(PowerSysconDriver::Shutdown);

	return B_OK;
}


status_t
PowerSysconDriver::Call()
{
	CHECK_RET(fSysconDevice->Write4(fOffset, fMask, fValue));
	return B_ERROR;
}


status_t
PowerSysconDriver::Shutdown(bool reboot)
{
	if (reboot && sRebootDriver != NULL) {
		return sRebootDriver->Call();
	} else if (!reboot && sPoweroffDriver != NULL) {
		return sPoweroffDriver->Call();
	}
	return B_ERROR;
}


static driver_module_info sSysconPoweroffDriverModule = {
	.info = {
		.name = SYSCON_POWEROFF_DRIVER_MODULE_NAME,
	},
	.probe = PowerSysconDriver::ProbePoweroff
};

static driver_module_info sSysconRebootDriverModule = {
	.info = {
		.name = SYSCON_REBOOT_DRIVER_MODULE_NAME,
	},
	.probe = PowerSysconDriver::ProbeReboot
};


_EXPORT module_info* modules[] = {
	(module_info* )&sSysconPoweroffDriverModule,
	(module_info* )&sSysconRebootDriverModule,
	NULL
};
