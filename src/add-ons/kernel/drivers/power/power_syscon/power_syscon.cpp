#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Syscon.h>

#include <AutoDeleter.h>
#include <AutoDeleterDM2.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define SYSCON_POWEROFF_DRIVER_MODULE_NAME "drivers/power/power_syscon/poweroff/driver/v1"
#define SYSCON_REBOOT_DRIVER_MODULE_NAME   "drivers/power/power_syscon/reboot/driver/v1"


class PowerSysconDriver: public DeviceDriver {
public:
	PowerSysconDriver(DeviceNode* node, bool isReboot): fNode(node), fIsReboot(isReboot) {}
	virtual ~PowerSysconDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, bool isReboot, DeviceDriver** driver);
	static status_t ProbePoweroff(DeviceNode* node, DeviceDriver** driver);
	static status_t ProbeReboot(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	status_t Call();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};
	DeviceNodePutter fSysconNode;
	SysconDevice* fSysconDevice {};

	uint32 fOffset {};
	uint32 fValue {};
	uint32 fMask = 0xffffffff;

	bool fIsReboot;
};


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
	dprintf("PowerSysconDriver::Init()\n");
	dprintf("  fIsReboot: %d\n", fIsReboot);

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	const void* prop;
	int propLen;
	prop = fFdtDevice->GetProp("regmap", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;
	uint32 regmapPhandle = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
	dprintf("  regmapPhandle: %" B_PRIu32 "\n", regmapPhandle);

	prop = fFdtDevice->GetProp("offset", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;
	fOffset = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
	dprintf("  fOffset: %" B_PRIu32 "\n", fOffset);

	prop = fFdtDevice->GetProp("value", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;
	fValue = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
	dprintf("  fValue: %" B_PRIu32 "\n", fValue);

	prop = fFdtDevice->GetProp("mask", &propLen);
	if (prop != NULL) {
		if (propLen != 4)
			return B_ERROR;

		fMask = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
		dprintf("  fMask: %" B_PRIu32 "\n", fMask);
	}

	DeviceNodePutter fdtBusNode(fFdtDevice->GetBus());
	FdtBus* fdtBus = fdtBusNode->QueryDriverInterface<FdtBus>();
	dprintf("  fdtBus: %p\n", fdtBus);

	fSysconNode.SetTo(fdtBus->NodeByPhandle(regmapPhandle));
	dprintf("  fSysconNode: %p\n", fSysconNode.Get());
	fSysconDevice = fSysconNode->QueryDriverInterface<SysconDevice>();
	dprintf("  fSysconDevice: %p\n", fSysconDevice);

	// TODO: register kernel interface

	return B_OK;
}


status_t
PowerSysconDriver::Call()
{
	CHECK_RET(fSysconDevice->Write4(fOffset, fMask, fValue));
	return B_OK;
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
