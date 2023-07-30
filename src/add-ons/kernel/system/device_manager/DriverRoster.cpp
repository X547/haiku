#include "DriverRoster.h"

#include <dm2/bus/FDT.h>
#include <dm2/bus/PCI.h>
#include <dm2/bus/Virtio.h>



DriverRoster DriverRoster::sInstance;


status_t
DriverModuleInfo::Init(DriverAddonInfo* addon, const char* name)
{
	fName.SetTo(strdup(name));
	if (!fName.IsSet())
		return B_NO_MEMORY;

	fAddon = addon;
	return B_OK;
}


status_t
DriverAddonInfo::Init(const char* path, const KMessage& msg)
{
	fPath.SetTo(strdup(path));
	if (!fPath.IsSet())
		return B_NO_MEMORY;

	return B_OK;
}


status_t
DriverRoster::Init()
{
	return B_OK;
}


void
DriverRoster::Lookup(DeviceNode* node, LookupResultArray& result)
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
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/common/null/driver/v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/common/zero/driver/v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "bus_managers/fdt/driver/v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "bus_managers/random/driver/v1"});
		} else if (strcmp(bus, "generic") == 0) {
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/disk/virtual/ram_disk/driver/v1"});
		}
	}
}
