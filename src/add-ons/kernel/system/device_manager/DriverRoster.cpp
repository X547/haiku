#include "DriverRoster.h"

#include <dm2/bus/FDT.h>
#include <dm2/bus/PCI.h>
#include <dm2/bus/Virtio.h>

#include "CompatInfoData.h"


DriverRoster DriverRoster::sInstance;


DriverCompatInfo::~DriverCompatInfo()
{
	while (!fChildInfos.IsEmpty()) {
		DriverCompatInfo* childInfo = fChildInfos.RemoveHead();
		delete childInfo;
	}
}


status_t
DriverCompatInfo::Init(DriverAddonInfo* addonInfo, const KMessage& msg)
{
	const char* moduleName;
	if (msg.FindString("module", &moduleName) >= B_OK) {
		CHECK_RET(addonInfo->AddModule(moduleName, fModuleInfo));
	}

	const void* data;
	int32 size;
	if (msg.FindData("score", B_FLOAT_TYPE, &data, &size) >= B_OK && size == sizeof(float))
		fScore = *(float*)data;

	KMessageField field;
	if (msg.FindField("attrs", B_MESSAGE_TYPE, &field) >= B_OK) {
		data = field.ElementAt(0, &size);
		CHECK_RET(fAttrs.SetTo((void*)data, size, 0, KMessage::KMESSAGE_INIT_FROM_BUFFER | KMessage::KMESSAGE_CLONE_BUFFER));
	}

	if (msg.FindField("driver", B_MESSAGE_TYPE, &field) >= B_OK) {
		for (int32 i = 0; i < field.CountElements(); i++) {
			int32 size;
			const void *data = field.ElementAt(i, &size);
			KMessage subMsg;
			subMsg.SetTo((void*)data, size);

			ObjectDeleter<DriverCompatInfo> childInfo(new(std::nothrow) DriverCompatInfo());
			if (!childInfo.IsSet())
				return B_NO_MEMORY;

			childInfo->fParentInfo = this;
			CHECK_RET(childInfo->Init(addonInfo, subMsg));
			fChildInfos.Insert(childInfo.Detach());
		}
	}

	return B_OK;
}


status_t
DriverModuleInfo::Init(DriverAddonInfo* addon, const char* name)
{
	fName.SetTo(strdup(name));
	if (!fName.IsSet())
		return B_NO_MEMORY;

	fAddon = addon;
	return B_OK;
}


DriverAddonInfo::~DriverAddonInfo()
{
	for (;;) {
		DriverModuleInfo *module = fModules.LeftMost();
		if (module == NULL)
			break;

		fModules.Remove(module);
		delete module;
	}
}


status_t
DriverAddonInfo::Init(const char* path, const KMessage& msg)
{
	dprintf("DriverAddonInfo::Init(\"%s\")\n", path);

	fPath.SetTo(strdup(path));
	if (!fPath.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fCompatInfo.Init(this, msg));

	return B_OK;
}


status_t
DriverAddonInfo::AddModule(const char* name, DriverModuleInfo*& outModule)
{
	dprintf("DriverAddonInfo::AddModule(\"%s\")\n", name);
	{
		DriverModuleInfo* module = fModules.Find(name);
		if (module != NULL)
			return B_OK;
	}
	ObjectDeleter<DriverModuleInfo> module(new(std::nothrow) DriverModuleInfo());
	if (!module.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(module->Init(this, name));
	fModules.Insert(module.Get());

	outModule = module.Detach();
	return B_OK;
}


status_t
DriverRoster::Init()
{
	for (int32 i = 0; i < kCompatInfoDataLen; i++) {
		const CompatInfoData& data = kCompatInfoData[i];
		KMessage msg;
		msg.SetTo(data.data);
		ObjectDeleter<DriverAddonInfo> driverAddonInfo(new(std::nothrow) DriverAddonInfo());
		if (!driverAddonInfo.IsSet())
			return B_NO_MEMORY;

		if (driverAddonInfo->Init(data.addonPath, msg) < B_OK) {
			dprintf("[!] DriverAddonInfo::Init(\"%s\") failed\n", data.addonPath);
			continue;
		}

		if (RegisterDriverAddon(driverAddonInfo.Detach()) < B_OK) {
			dprintf("[!] RegisterDriverAddon(\"%s\") failed\n", data.addonPath);
		}
	}

	return B_OK;
}


status_t
DriverRoster::RegisterDriverAddon(DriverAddonInfo* driverAddonPtr)
{
	ObjectDeleter<DriverAddonInfo> driverAddon(driverAddonPtr);

	if (fDriverAddons.Find(driverAddonPtr->GetPath()) != NULL)
		return EEXIST;

	fDriverAddons.Insert(driverAddon.Detach());
	return B_OK;
}


void
DriverRoster::LookupFixed(DeviceNode* node, LookupResultArray& result)
{
	// TODO: implement compatible hardware information from driver add-on resources

	result.MakeEmpty();
	const char* bus;
	if (node->FindAttrString(B_DEVICE_BUS, &bus) >= B_OK) {
		if (strcmp(bus, "fdt") == 0) {
			const char* compatible;
			if (node->FindAttrString(B_FDT_DEVICE_COMPATIBLE, &compatible) >= B_OK) {
				if (strcmp(compatible, "riscv,plic0") == 0
					|| strcmp(compatible, "sifive,fu540-c000-plic") == 0
					|| strcmp(compatible, "sifive,plic-1.0.0") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "interrupt_controllers/plic/driver/v1"});
				} else if (strcmp(compatible, "pci-host-ecam-generic") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "busses/pci/ecam/driver/v1"});
				} else if (strcmp(compatible, "google,goldfish-rtc") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "rtc/goldfish/driver/v1"});
				} else if (strcmp(compatible, "syscon-poweroff") == 0
					|| strcmp(compatible, "syscon-reboot") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "power/syscon/driver/v1"});
				} else if (strcmp(compatible, "opencores,i2c-ocores") == 0
					|| strcmp(compatible, "sifive,fu740-c000-i2c") == 0
					|| strcmp(compatible, "sifive,i2c0") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "busses/i2c/ocores_i2c/driver/v1"});
				} else if (strcmp(compatible, "hid-over-i2c") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/input/i2c_hid/driver/v1"});
				} else if (strcmp(compatible, "virtio,mmio") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "busses/virtio/virtio_mmio/driver/v1"});
				}
			}
		} else if (strcmp(bus, "pci") == 0) {
			uint16 baseClass, subClass;
			if (node->FindAttrUint16(B_PCI_DEVICE_TYPE, &baseClass) >= B_OK
				&& node->FindAttrUint16(B_PCI_DEVICE_SUB_TYPE, &subClass) >= B_OK) {
				if (baseClass == PCI_mass_storage && subClass == PCI_nvm) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/disk/nvme_disk/driver/v1"});
				}
			}
		} else if (strcmp(bus, "virtio") == 0) {
			uint16 type;
			if (node->FindAttrUint16(VIRTIO_DEVICE_TYPE_ITEM, &type) >= B_OK) {
				switch (type) {
					case VIRTIO_DEVICE_ID_NETWORK:
						result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/network/virtio_net/driver/v1"});
						break;
					case VIRTIO_DEVICE_ID_BLOCK:
						result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/disk/virtual/virtio_block/driver/v1"});
						break;
					case VIRTIO_DEVICE_ID_INPUT:
						result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/input/virtio_input/driver/v1"});
						break;
				}
			}
		} else if (strcmp(bus, "root") == 0) {
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/null/driver/v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/zero/driver/v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "bus_managers/fdt/driver/v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "bus_managers/random/driver/v1"});
		} else if (strcmp(bus, "generic") == 0) {
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/disk/virtual/ram_disk/driver/v1"});
		}
	}
}


void
DriverRoster::Lookup(DeviceNode* node, LookupResultArray& result)
{
	return LookupFixed(node, result);
}
