/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "ecam.h"
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
EcamPciController::SupportsDevice(device_node* parent)
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
EcamPciController::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "ECAM PCI Host Controller"} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/pci/root/driver_v1"} },
		{}
	};

	return gDeviceManager->register_node(parent, ECAM_PCI_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


status_t
EcamPciController::InitDriver(device_node* node, EcamPciController*& outDriver)
{
	ObjectDeleter<EcamPciController> driver(new(std::nothrow) EcamPciController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
EcamPciController::InitDriverInt(device_node* node)
{
	fNode = node;
	dprintf("+EcamPciController::InitDriver()\n");

	DeviceNodePutter<&gDeviceManager> parent(gDeviceManager->get_parent_node(node));

	const char* bus;
	CHECK_RET(gDeviceManager->get_attr_string(parent.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(gDeviceManager->get_driver(parent.Get(),
		(driver_module_info**)&fdtModule, (void**)&fdtDev));

	const void* prop;
	int propLen;
#if 0
	prop = fdtModule->get_prop(fdtDev, "bus-range", &propLen);
	if (prop != NULL && propLen == 8)
		fBusCount = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 1)) + 1;
#endif
	dprintf("  busCount: %" B_PRIu32 "\n", fBusCount);

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
	} else {
		dprintf("  ranges:\n");
		for (uint32_t *it = (uint32_t*)prop; (uint8_t*)it - (uint8_t*)prop < propLen; it += 7) {
			dprintf("    ");
			uint32_t kind = B_BENDIAN_TO_HOST_INT32(*(it + 0));
			uint64_t childAdr = B_BENDIAN_TO_HOST_INT64(*(uint64_t*)(it + 1));
			uint64_t parentAdr = B_BENDIAN_TO_HOST_INT64(*(uint64_t*)(it + 3));
			uint64_t len = B_BENDIAN_TO_HOST_INT64(*(uint64_t*)(it + 5));

			switch (kind & 0x03000000) {
			case 0x01000000:
				SetRegisterRange(kRegIo, parentAdr, childAdr, len);
				break;
			case 0x02000000:
				SetRegisterRange(kRegMmio32, parentAdr, childAdr, len);
				break;
			case 0x03000000:
				SetRegisterRange(kRegMmio64, parentAdr, childAdr, len);
				break;
			}

			switch (kind & 0x03000000) {
			case 0x00000000: dprintf("CONFIG"); break;
			case 0x01000000: dprintf("IOPORT"); break;
			case 0x02000000: dprintf("MMIO32"); break;
			case 0x03000000: dprintf("MMIO64"); break;
			}

			dprintf(" (0x%08" B_PRIx32 "): ", kind);
			dprintf("child: %08" B_PRIx64, childAdr);
			dprintf(", parent: %08" B_PRIx64, parentAdr);
			dprintf(", len: %" B_PRIx64 "\n", len);
		}
	}

	uint64 regs = 0;
	if (!fdtModule->get_reg(fdtDev, 0, &regs, &fRegsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("ECAM I2C MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	dprintf("-EcamPciController::InitDriver()\n");
	return B_OK;
}


void
EcamPciController::UninitDriver()
{
	delete this;
}


void
EcamPciController::SetRegisterRange(int kind, phys_addr_t parentBase, phys_addr_t childBase,
	size_t size)
{
	RegisterRange& range = fRegisterRanges[kind];

	range.parentBase = parentBase;
	range.childBase = childBase;
	range.size = size;
	// Avoid allocating zero address.
	range.free = (childBase != 0) ? childBase : 1;
}


addr_t
EcamPciController::ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset)
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
EcamPciController::ReadConfig(uint8 bus, uint8 device, uint8 function,
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
EcamPciController::WriteConfig(uint8 bus, uint8 device, uint8 function,
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
EcamPciController::GetMaxBusDevices(int32& count)
{
	count = fBusCount;
	return B_OK;
}


status_t
EcamPciController::ReadIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8& irq)
{
	return B_UNSUPPORTED;
}


status_t
EcamPciController::WriteIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}
