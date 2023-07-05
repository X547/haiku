/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#include <string.h>
#include <new>

#include <KernelExport.h>
#include <ByteOrder.h>
#include <bus/FDT.h>
#include <clock.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define CLOCK_FIXED_MODULE_NAME "drivers/clock/fixed/driver_v1"


static device_manager_info *sDeviceManager;


class FixedClockController {
private:
	int64 fRate = 0;

	inline status_t InitDriverInt(device_node* node);

public:
	virtual ~FixedClockController() = default;

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, FixedClockController*& driver);
	void UninitDriver();

	int32 IsEnabled(int32 id);
	int64 GetRate(int32 id);
};


float
FixedClockController::SupportsDevice(device_node* parent)
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

	if (strcmp(compatible, "fixed-clock") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
FixedClockController::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Fixed Clock"} },
		{}
	};

	return sDeviceManager->register_node(parent, CLOCK_FIXED_MODULE_NAME, attrs, NULL, NULL);
}


status_t
FixedClockController::InitDriver(device_node* node, FixedClockController*& outDriver)
{
	ObjectDeleter<FixedClockController> driver(new(std::nothrow) FixedClockController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
FixedClockController::InitDriverInt(device_node* node)
{
	dprintf("FixedClockController::InitDriver\n");

	DeviceNodePutter<&sDeviceManager> fdtNode(sDeviceManager->get_parent_node(node));

	const char* bus;
	CHECK_RET(sDeviceManager->get_attr_string(fdtNode.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(sDeviceManager->get_driver(fdtNode.Get(), (driver_module_info**)&fdtModule,
		(void**)&fdtDev));

	const void* prop;
	int propLen;
	prop = fdtModule->get_prop(fdtDev, "clock-frequency", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;

	fRate = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
	dprintf("  frequency: %" B_PRId64 "\n", fRate);

	return B_OK;
}


void
FixedClockController::UninitDriver()
{
	dprintf("FixedClockController::UninitDriver\n");

	delete this;
}


int32
FixedClockController::IsEnabled(int32 id)
{
	if (id != 0)
		return ENOENT;

	return true;
}


int64
FixedClockController::GetRate(int32 id)
{
	if (id != 0)
		return ENOENT;

	return fRate;
}


static clock_device_module_info sControllerModuleInfo = {
	{
		{
			.name = CLOCK_FIXED_MODULE_NAME,
		},
		.supports_device = FixedClockController::SupportsDevice,
		.register_device = FixedClockController::RegisterDevice,
		.init_driver = [](device_node* node, void** driverCookie) {
			return FixedClockController::InitDriver(node,
				*(FixedClockController**)driverCookie);
		},
		.uninit_driver = [](void* driverCookie) {
			return static_cast<FixedClockController*>(driverCookie)->UninitDriver();
		},
	},
	.is_enabled = [](struct clock_device* dev, int32 id) {
		return reinterpret_cast<FixedClockController*>(dev)->IsEnabled(id);
	},
	.get_rate = [](struct clock_device* dev, int32 id) {
		return reinterpret_cast<FixedClockController*>(dev)->GetRate(id);
	},
};

_EXPORT module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&sControllerModuleInfo,
	NULL
};
