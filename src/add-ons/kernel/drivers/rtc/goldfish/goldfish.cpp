/*
 * Copyright 2022-2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <real_time_clock.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define GOLDFISH_RTC_DRIVER_MODULE_NAME "drivers/rtc/goldfish/driver/v1"


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


class GoldfishRtcDriver: public DeviceDriver, public RealTimeClock {
public:
	GoldfishRtcDriver(DeviceNode* node): fNode(node) {}
	virtual ~GoldfishRtcDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	// RealTimeClock
	uint32 GetHwTime() final;
	void SetHwTime(uint32 seconds) final;

private:
	status_t Init();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	GoldfishRtcRegs volatile* fRegs {};

	bool fIsHookSet = false;
};


GoldfishRtcDriver::~GoldfishRtcDriver()
{
	if (fIsHookSet)
		rtc_set_hook(NULL);
}


status_t
GoldfishRtcDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<GoldfishRtcDriver> driver(new(std::nothrow) GoldfishRtcDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
GoldfishRtcDriver::Init()
{
	dprintf("GoldfishRtcDriver::Init()\n");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fFdtDevice->GetReg(0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("Goldfish MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	rtc_set_hook(this);
	fIsHookSet = true;

	return B_OK;
}


uint32
GoldfishRtcDriver::GetHwTime()
{
	uint64 time = fRegs->timeLo + ((uint64)fRegs->timeHi << 32);
	return time / 1000000000ULL;
}


void
GoldfishRtcDriver::SetHwTime(uint32 seconds)
{
	uint64 time = seconds * 1000000000ULL;
	fRegs->timeLo = (uint32)time;
	fRegs->timeHi = (uint32)(time >> 32);
}


static driver_module_info sGoldfishRtcDriverModule = {
	.info = {
		.name = GOLDFISH_RTC_DRIVER_MODULE_NAME,
	},
	.probe = GoldfishRtcDriver::Probe
};

_EXPORT module_info* modules[] = {
	(module_info* )&sGoldfishRtcDriverModule,
	NULL
};
