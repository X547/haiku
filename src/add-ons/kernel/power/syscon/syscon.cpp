/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <bus/FDT.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDrivers.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define SYSCON_MODULE_NAME	"power/syscon/driver_v1"


static device_manager_info *sDeviceManager;

struct SysconRegs {
	uint32 stub;
};

class Syscon {
private:
	AreaDeleter fRegsArea;
	SysconRegs volatile* fRegs {};

	inline status_t InitDriverInt(device_node* node);

public:
	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, Syscon*& driver);
	void UninitDriver();
};


float
Syscon::SupportsDevice(device_node* parent)
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

	if (strcmp(compatible, "syscon-poweroff") != 0 &&
		strcmp(compatible, "syscon-reboot") != 0)
		return 0.0f;

	dprintf("Syscon::SupportsDevice(%p)\n", parent);
	return 1.0f;
}


status_t
Syscon::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Syscon"} },
		{}
	};

	return sDeviceManager->register_node(parent, SYSCON_MODULE_NAME, attrs, NULL, NULL);
}


status_t
Syscon::InitDriver(device_node* node, Syscon*& outDriver)
{
	ObjectDeleter<Syscon> driver(new(std::nothrow) Syscon());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


template<typename ModuleInfo, typename Cookie>
struct DevNodeRef {
	device_node* node;
	DeviceNodePutter<&sDeviceManager> nodePutter;
	ModuleInfo* module;
	Cookie* cookie;
	status_t res;

	DevNodeRef(device_node* inNode, const char* busName = NULL, bool acquireRef = true): node(inNode)
	{
		if (acquireRef)
			nodePutter.SetTo(node);

		if (node == NULL) {
			res = B_ERROR;
			return;
		}
		if (busName != NULL) {
			const char* bus;
			res = sDeviceManager->get_attr_string(node, B_DEVICE_BUS, &bus, false);
			if (res < B_OK)
				return;
			if (strcmp(bus, busName) != 0) {
				res = B_ERROR;
				return;
			}
		}
		res = sDeviceManager->get_driver(node, (driver_module_info**)&module, (void**)&cookie);
	}
};


status_t
Syscon::InitDriverInt(device_node* node)
{
	dprintf("Syscon::InitDriver(%p)\n", node);
	DevNodeRef<fdt_device_module_info, fdt_device> fdtDev(sDeviceManager->get_parent_node(node), "fdt");
	CHECK_RET(fdtDev.res);
	dprintf("  (1)\n");

	const void* prop;
	int propLen;
	prop = fdtDev.module->get_prop(fdtDev.cookie, "regmap", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;
	int regmapPhandle = B_BENDIAN_TO_HOST_INT32(*(uint32*)prop);
	dprintf("  regmapPhandle: %d\n", regmapPhandle);

	prop = fdtDev.module->get_prop(fdtDev.cookie, "offset", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;
	int regmapOffset = B_BENDIAN_TO_HOST_INT32(*(uint32*)prop);
	dprintf("  regmapOffset: %#x\n", regmapOffset);

	prop = fdtDev.module->get_prop(fdtDev.cookie, "value", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;
	int regmapValue = B_BENDIAN_TO_HOST_INT32(*(uint32*)prop);
	dprintf("  regmapValue: %#x\n", regmapValue);

	DevNodeRef<fdt_bus_module_info, fdt_bus> fdtBus(fdtDev.module->get_bus(fdtDev.cookie), NULL, false);
	CHECK_RET(fdtBus.res);
	dprintf("  (2)\n");

	DevNodeRef<fdt_device_module_info, fdt_device> sysconFdtDev(fdtBus.module->node_by_phandle(fdtBus.cookie, regmapPhandle), "fdt", false);
	CHECK_RET(sysconFdtDev.res);
	dprintf("  (3)\n");

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!sysconFdtDev.module->get_reg(sysconFdtDev.cookie, 0, &regs, &regsLen))
		return B_ERROR;
	dprintf("  (4)\n");

	fRegsArea.SetTo(map_physical_memory("Syscon MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();
	dprintf("  (5)\n");

	return B_OK;
}


void
Syscon::UninitDriver()
{
	delete this;
}


static driver_module_info sControllerModuleInfo = {
	{
		.name = SYSCON_MODULE_NAME,
	},
	.supports_device = [](device_node* parent) {
		return Syscon::SupportsDevice(parent);
	},
	.register_device = [](device_node* parent) {
		return Syscon::RegisterDevice(parent);
	},
	.init_driver = [](device_node* node, void** driverCookie) {
		return Syscon::InitDriver(node, *(Syscon**)driverCookie);
	},
	.uninit_driver = [](void* driverCookie) {
		return static_cast<Syscon*>(driverCookie)->UninitDriver();
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
