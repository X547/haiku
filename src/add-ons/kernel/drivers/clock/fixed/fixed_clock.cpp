#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Clock.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define FIXED_CLOCK_DRIVER_MODULE_NAME "drivers/clock/fixed_clock/driver/v1"


class FixedClockDriver: public DeviceDriver, public ClockController, public ClockDevice {
public:
	FixedClockDriver(DeviceNode* node): fNode(node) {}
	virtual ~FixedClockDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// ClockController
	ClockDevice* GetDevice(const uint8* optInfo, uint32 optInfoSize) final;

	// ClockDevice
	DeviceNode* OwnerNode() final;

	bool IsEnabled() const final;
	status_t SetEnabled(bool doEnable) final;

	int64 GetRate() const final;
	int64 SetRate(int64 rate) final;
	int64 SetRateDry(int64 rate) const final;

	ClockDevice* GetParent() const final;
	status_t SetParent(ClockDevice* parent) final;

private:
	status_t Init();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};
	uint32 fRate;
};


status_t
FixedClockDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<FixedClockDriver> driver(new(std::nothrow) FixedClockDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
FixedClockDriver::Init()
{
	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	const void* prop;
	int propLen;
	prop = fFdtDevice->GetProp("clock-frequency", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;

	fRate = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);

	return B_OK;
}


void*
FixedClockDriver::QueryInterface(const char* name)
{
	if (strcmp(name, ClockController::ifaceName) == 0)
		return static_cast<ClockController*>(this);

	return NULL;
}


ClockDevice*
FixedClockDriver::GetDevice(const uint8* optInfo, uint32 optInfoSize)
{
	if (optInfoSize != 0)
		return NULL;

	return static_cast<ClockDevice*>(this);
}


DeviceNode*
FixedClockDriver::OwnerNode()
{
	fNode->AcquireReference();
	return fNode;
}


bool
FixedClockDriver::IsEnabled() const
{
	return true;
}


status_t
FixedClockDriver::SetEnabled(bool doEnable)
{
	return B_OK;
}


int64
FixedClockDriver::GetRate() const
{
	return fRate;
}


int64
FixedClockDriver::SetRate(int64 rate)
{
	return ENOSYS;
}


int64
FixedClockDriver::SetRateDry(int64 rate) const
{
	return ENOSYS;
}


ClockDevice*
FixedClockDriver::GetParent() const
{
	return NULL;
}


status_t
FixedClockDriver::SetParent(ClockDevice* parent)
{
	return ENOSYS;
}


static driver_module_info sFixedClockDriverModule = {
	.info = {
		.name = FIXED_CLOCK_DRIVER_MODULE_NAME,
	},
	.probe = FixedClockDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sFixedClockDriverModule,
	NULL
};
