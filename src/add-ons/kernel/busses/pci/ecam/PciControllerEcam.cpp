/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "PciControllerEcam.h"
#include <bus/FDT.h>

#include <AutoDeleterDrivers.h>

#include <string.h>
#include <new>


static uint32
ReadReg8(addr_t adr)
{
	uint32 ofs = adr % 4;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint8 out[4];
	} val{.in = *(vuint32*)adr};
	return val.out[ofs];
}


static uint32
ReadReg16(addr_t adr)
{
	uint32 ofs = adr / 2 % 2;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint16 out[2];
	} val{.in = *(vuint32*)adr};
	return val.out[ofs];
}


static void
WriteReg8(addr_t adr, uint32 value)
{
	uint32 ofs = adr % 4;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint8 out[4];
	} val{.in = *(vuint32*)adr};
	val.out[ofs] = (uint8)value;
	*(vuint32*)adr = val.in;
}


static void
WriteReg16(addr_t adr, uint32 value)
{
	uint32 ofs = adr / 2 % 2;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint16 out[2];
	} val{.in = *(vuint32*)adr};
	val.out[ofs] = (uint16)value;
	*(vuint32*)adr = val.in;
}


//#pragma mark - driver


float
PciControllerEcam::SupportsDevice(device_node* parent)
{
	const char* bus;
	status_t status = gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	const char* compatible;
	status = gDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "pci-host-ecam-generic") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
PciControllerEcam::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "ECAM PCI Host Controller"} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/pci/root/driver_v1"} },
		{}
	};

	return gDeviceManager->register_node(parent, ECAM_PCI_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


status_t
PciControllerEcam::InitDriver(device_node* node, PciControllerEcam*& outDriver)
{
	ObjectDeleter<PciControllerEcam> driver(new(std::nothrow) PciControllerEcam());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
PciControllerEcam::ReadResourceInfo()
{
	DeviceNodePutter<&gDeviceManager> fdtNode(gDeviceManager->get_parent_node(fNode));

	const char* bus;
	CHECK_RET(gDeviceManager->get_attr_string(fdtNode.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(gDeviceManager->get_driver(fdtNode.Get(),
		(driver_module_info**)&fdtModule, (void**)&fdtDev));

	const void* prop;
	int propLen;

	prop = fdtModule->get_prop(fdtDev, "bus-range", &propLen);
	if (prop != NULL && propLen == 8) {
		uint32 busBeg = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 0));
		uint32 busEnd = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 1));
		dprintf("  bus-range: %" B_PRIu32 " - %" B_PRIu32 "\n", busBeg, busEnd);
	}

	prop = fdtModule->get_prop(fdtDev, "interrupt-map-mask", &propLen);
	if (prop == NULL || propLen != 4 * 4) {
		dprintf("  \"interrupt-map-mask\" property not found or invalid");
		return B_ERROR;
	}
	fInterruptMapMask.childAdr = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 0));
	fInterruptMapMask.childIrq = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 3));

	prop = fdtModule->get_prop(fdtDev, "interrupt-map", &propLen);
	fInterruptMapLen = (uint32)propLen / (6 * 4);
	fInterruptMap.SetTo(new(std::nothrow) InterruptMap[fInterruptMapLen]);
	if (!fInterruptMap.IsSet())
		return B_NO_MEMORY;

	for (uint32_t *it = (uint32_t*)prop; (uint8_t*)it - (uint8_t*)prop < propLen; it += 6) {
		size_t i = (it - (uint32_t*)prop) / 6;

		fInterruptMap[i].childAdr = B_BENDIAN_TO_HOST_INT32(*(it + 0));
		fInterruptMap[i].childIrq = B_BENDIAN_TO_HOST_INT32(*(it + 3));
		fInterruptMap[i].parentIrqCtrl = B_BENDIAN_TO_HOST_INT32(*(it + 4));
		fInterruptMap[i].parentIrq = B_BENDIAN_TO_HOST_INT32(*(it + 5));
	}

	dprintf("  interrupt-map:\n");
	for (size_t i = 0; i < fInterruptMapLen; i++) {
		dprintf("    ");
		// child unit address
		PciAddress pciAddress{.val = fInterruptMap[i].childAdr};
		dprintf("bus: %" B_PRIu32, pciAddress.bus);
		dprintf(", dev: %" B_PRIu32, pciAddress.device);
		dprintf(", fn: %" B_PRIu32, pciAddress.function);

		dprintf(", childIrq: %" B_PRIu32, fInterruptMap[i].childIrq);
		dprintf(", parentIrq: (%" B_PRIu32, fInterruptMap[i].parentIrqCtrl);
		dprintf(", %" B_PRIu32, fInterruptMap[i].parentIrq);
		dprintf(")\n");
		if (i % 4 == 3 && (i + 1 < fInterruptMapLen))
			dprintf("\n");
	}

	prop = fdtModule->get_prop(fdtDev, "ranges", &propLen);
	if (prop == NULL) {
		dprintf("  \"ranges\" property not found");
		return B_ERROR;
	}
	dprintf("  ranges:\n");
	for (uint32_t *it = (uint32_t*)prop; (uint8_t*)it - (uint8_t*)prop < propLen; it += 7) {
		dprintf("    ");
		uint32_t type      = B_BENDIAN_TO_HOST_INT32(*(it + 0));
		uint64_t childAdr  = B_BENDIAN_TO_HOST_INT64(*(uint64_t*)(it + 1));
		uint64_t parentAdr = B_BENDIAN_TO_HOST_INT64(*(uint64_t*)(it + 3));
		uint64_t len       = B_BENDIAN_TO_HOST_INT64(*(uint64_t*)(it + 5));

		uint32 outType = kPciRangeInvalid;
		switch (type & fdtPciRangeTypeMask) {
		case fdtPciRangeIoPort:
			outType = kPciRangeIoPort;
			break;
		case fdtPciRangeMmio32Bit:
			outType = kPciRangeMmio;
			break;
		case fdtPciRangeMmio64Bit:
			outType = kPciRangeMmio + kPciRangeMmio64Bit;
			break;
		}
		if (outType >= kPciRangeMmio && outType < kPciRangeMmioEnd
			&& (fdtPciRangePrefechable & type) != 0)
			outType += kPciRangeMmioPrefetch;

		if (outType != kPciRangeInvalid) {
			fResourceRanges[outType].type = outType;
			fResourceRanges[outType].host_addr = parentAdr;
			fResourceRanges[outType].pci_addr = childAdr;
			fResourceRanges[outType].size = len;
		}

		switch (type & fdtPciRangeTypeMask) {
		case fdtPciRangeConfig:    dprintf("CONFIG"); break;
		case fdtPciRangeIoPort:    dprintf("IOPORT"); break;
		case fdtPciRangeMmio32Bit: dprintf("MMIO32"); break;
		case fdtPciRangeMmio64Bit: dprintf("MMIO64"); break;
		}

		dprintf(" (0x%08" B_PRIx32 "): ", type);
		dprintf("child: %08" B_PRIx64, childAdr);
		dprintf(", parent: %08" B_PRIx64, parentAdr);
		dprintf(", len: %" B_PRIx64 "\n", len);
	}
	return B_OK;
}


status_t
PciControllerEcam::InitDriverInt(device_node* node)
{
	fNode = node;
	dprintf("+PciControllerEcam::InitDriver()\n");

	CHECK_RET(ReadResourceInfo());

	DeviceNodePutter<&gDeviceManager> fdtNode(gDeviceManager->get_parent_node(node));

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(gDeviceManager->get_driver(fdtNode.Get(),
		(driver_module_info**)&fdtModule, (void**)&fdtDev));

	uint64 regs = 0;
	if (!fdtModule->get_reg(fdtDev, 0, &regs, &fRegsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("PCI Config MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	dprintf("-PciControllerEcam::InitDriver()\n");
	return B_OK;
}


void
PciControllerEcam::UninitDriver()
{
	delete this;
}


addr_t
PciControllerEcam::ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	PciAddressEcam address {
		.offset = offset,
		.function = function,
		.device = device,
		.bus = bus
	};
	PciAddressEcam addressEnd = address;
	addressEnd.offset = /*~(uint32)0*/ 4095;

	if (addressEnd.val >= fRegsLen)
		return 0;

	return (addr_t)fRegs + address.val;
}


//#pragma mark - PCI controller


status_t
PciControllerEcam::ReadConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32& value)
{
	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: value = ReadReg8(address); break;
		case 2: value = ReadReg16(address); break;
		case 4: value = *(vuint32*)address; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


status_t
PciControllerEcam::WriteConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: WriteReg8(address, value); break;
		case 2: WriteReg16(address, value); break;
		case 4: *(vuint32*)address = value; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


status_t
PciControllerEcam::GetMaxBusDevices(int32& count)
{
	count = 32;
	return B_OK;
}


status_t
PciControllerEcam::ReadIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8& irq)
{
	return B_UNSUPPORTED;
}


status_t
PciControllerEcam::WriteIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}


status_t
PciControllerEcam::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= kPciRangeEnd)
		return B_BAD_INDEX;

	*range = fResourceRanges[index];
	return B_OK;

#if 0
	uint32 type = kPciRangeInvalid;
	for (;;) {
		while (fResourceRanges[type].size == 0) {
			type++;
			if (type == kPciRangeEnd)
				return B_BAD_INDEX;
		}
		if (index == 0)
			break;

		index--;
	}
	*range = fResourceRanges[type];
	return B_OK;
#endif
}
