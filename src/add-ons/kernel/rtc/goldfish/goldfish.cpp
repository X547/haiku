/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <bus/FDT.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDrivers.h>
#include <real_time_clock.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define GOLDFISH_MODULE_NAME	"rtc/goldfish/driver_v1"


static device_manager_info *sDeviceManager;

struct GoldfishRtcRegs {
	uint32 timeLo;
	uint32 timeHi;
	uint32 alarmLo;
	uint32 alarmHi;
	uint32 irqEnabled;
	uint32 alarmClear;
	uint32 alarmStatus;
	uint32 irqClear;
};

class GoldfishRealTimeClock: public RealTimeClock {
private:
	AreaDeleter fRegsArea;
	GoldfishRtcRegs volatile* fRegs {};

	inline status_t InitDriverInt(device_node* node);

public:
	virtual ~GoldfishRealTimeClock() = default;

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, GoldfishRealTimeClock*& driver);
	void UninitDriver();

	uint32 GetHwTime() final;
	void SetHwTime(uint32 seconds) final;
};


float
GoldfishRealTimeClock::SupportsDevice(device_node* parent)
{
	const char* bus;
	status_t status = sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	const char* compatible;
	status = sDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "google,goldfish-rtc") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
GoldfishRealTimeClock::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Goldfish RTC"} },
		{}
	};

	return sDeviceManager->register_node(parent, GOLDFISH_MODULE_NAME, attrs, NULL, NULL);
}


status_t
GoldfishRealTimeClock::InitDriver(device_node* node, GoldfishRealTimeClock*& outDriver)
{
	ObjectDeleter<GoldfishRealTimeClock> driver(new(std::nothrow) GoldfishRealTimeClock());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
GoldfishRealTimeClock::InitDriverInt(device_node* node)
{
	DeviceNodePutter<&sDeviceManager> fdtNode(sDeviceManager->get_parent_node(node));

	const char* bus;
	CHECK_RET(sDeviceManager->get_attr_string(fdtNode.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(sDeviceManager->get_driver(fdtNode.Get(), (driver_module_info**)&fdtModule, (void**)&fdtDev));

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fdtModule->get_reg(fdtDev, 0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("Goldfish MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	rtc_set_hook(this);

	return B_OK;
}


void
GoldfishRealTimeClock::UninitDriver()
{
	rtc_set_hook(NULL);
	delete this;
}


uint32
GoldfishRealTimeClock::GetHwTime()
{
	uint64 time = fRegs->timeLo + ((uint64)fRegs->timeHi << 32);
	return time / 1000000000ULL;
}


void
GoldfishRealTimeClock::SetHwTime(uint32 seconds)
{
	uint64 time = seconds * 1000000000ULL;
	fRegs->timeLo = (uint32)time;
	fRegs->timeHi = (uint32)(time >> 32);
}


static driver_module_info sControllerModuleInfo = {
	{
		.name = GOLDFISH_MODULE_NAME,
	},
	.supports_device = [](device_node* parent) {
		return GoldfishRealTimeClock::SupportsDevice(parent);
	},
	.register_device = [](device_node* parent) {
		return GoldfishRealTimeClock::RegisterDevice(parent);
	},
	.init_driver = [](device_node* node, void** driverCookie) {
		return GoldfishRealTimeClock::InitDriver(node, *(GoldfishRealTimeClock**)driverCookie);
	},
	.uninit_driver = [](void* driverCookie) {
		return static_cast<GoldfishRealTimeClock*>(driverCookie)->UninitDriver();
	}
};

_EXPORT module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&sControllerModuleInfo,
	NULL
};
