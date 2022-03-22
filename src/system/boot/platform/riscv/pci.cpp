#include "pci.h"
#include <KernelExport.h>
#include <kernel.h>
#include <drivers/PCI.h>
#include <ByteOrder.h>
#include <AutoDeleter.h>

#include <string.h>
#include <new>

static PciInitInfo sPciInitInfo{};

enum {
	kRegIo,
	kRegMmio32,
	kRegMmio64,
};

struct {
	phys_addr_t parentBase;
	phys_addr_t childBase;
	size_t size;
	phys_addr_t free;
} sPCIeRegisterRanges[3];

struct {
	uint32_t childAdr;
	uint32_t childIrq;
} sInterruptMapMask;

struct InterruptMap {
	uint32_t childAdr;
	uint32_t childIrq;
	uint32_t parentIrqCtrl;
	uint32_t parentIrq;
};

ArrayDeleter<InterruptMap> sInterruptMap;
uint32_t sInterruptMapLen = 0;


static void
SetRegisterRange(int kind, phys_addr_t parentBase, phys_addr_t childBase,
	size_t size)
{
	auto& range = sPCIeRegisterRanges[kind];

	range.parentBase = parentBase;
	range.childBase = childBase;
	range.size = size;
	// Avoid allocating zero address.
	range.free = (childBase != 0) ? childBase : 1;
}


static phys_addr_t
AllocRegister(int kind, size_t size)
{
	auto& range = sPCIeRegisterRanges[kind];

	phys_addr_t adr = ROUNDUP(range.free, size);
	if (adr - range.childBase + size > range.size)
		return 0;

	range.free = adr + size;

	return adr;
}


static InterruptMap*
LookupInterruptMap(uint32_t childAdr, uint32_t childIrq)
{
	childAdr &= sInterruptMapMask.childAdr;
	childIrq &= sInterruptMapMask.childIrq;
	for (uint32 i = 0; i < sInterruptMapLen; i++) {
		if ((sInterruptMap[i].childAdr) == childAdr
			&& (sInterruptMap[i].childIrq) == childIrq)
			return &sInterruptMap[i];
	}
	return NULL;
}


static uint32
EncodePciAddress(uint8 bus, uint8 device, uint8 function)
{
	return bus % (1 << 8) * (1 << 16)
		+ device % (1 << 5) * (1 << 11)
		+ function % (1 << 3) * (1 << 8);
}

static void
DecodePciAddress(uint32_t adr, uint8& bus, uint8& device, uint8& function)
{
	bus = adr / (1 << 16) % (1 << 8);
	device = adr / (1 << 11) % (1 << 5);
	function = adr / (1 << 8) % (1 << 3);
}


static inline addr_t
PciConfigAdr(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	addr_t address = sPciInitInfo.configRegs.start + EncodePciAddress(bus, device, function) * (1 << 4) + offset;
	if (address < sPciInitInfo.configRegs.start
		|| address /*+ size*/ > sPciInitInfo.configRegs.start + sPciInitInfo.configRegs.size)
		return 0;

	return address;
}


static uint32 ReadReg8(addr_t adr)
{
	uint32 ofs = adr % 4;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint8 out[4];
	} val{.in = *(vuint32*)adr};
	return val.out[ofs];
}

static uint32 ReadReg16(addr_t adr)
{
	uint32 ofs = adr / 2 % 2;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint16 out[2];
	} val{.in = *(vuint32*)adr};
	return val.out[ofs];
}

static void WriteReg8(addr_t adr, uint32 value)
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

static void WriteReg16(addr_t adr, uint32 value)
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

static status_t
read_pci_config(void *cookie, uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 *value)
{
	addr_t address = PciConfigAdr(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: *value = ReadReg8(address); break;
		case 2: *value = ReadReg16(address); break;
		case 4: *value = *(uint32*)address; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


static status_t
write_pci_config(void *cookie, uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	addr_t address = PciConfigAdr(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: WriteReg8(address, value); break;
		case 2: WriteReg16(address, value); break;
		case 4: *(uint32*)address = value; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}



static void
AllocRegsForDevice(uint8 bus, uint8 device, uint8 function)
{
	dprintf("AllocRegsForDevice(bus: %d, device: %d, function: %d)\n", bus, device, function);

	bool allocBars = true;

	uint32 vendorID = 0, deviceID = 0;
	read_pci_config(NULL, bus, device, function, PCI_vendor_id, 2, &vendorID);
	read_pci_config(NULL, bus, device, function, PCI_device_id, 2, &deviceID);
	dprintf("  vendorID: %#04" B_PRIx32 "\n", vendorID);
	dprintf("  deviceID: %#04" B_PRIx32 "\n", deviceID);

	uint32 headerType = 0;
	read_pci_config(NULL, bus, device, function,
		PCI_header_type, 1, &headerType);
	headerType = headerType % 0x80;

	dprintf("  headerType: ");
	switch (headerType) {
		case PCI_header_type_generic: dprintf("generic"); break;
		case PCI_header_type_PCI_to_PCI_bridge: dprintf("bridge"); break;
		case PCI_header_type_cardbus: dprintf("cardbus"); break;
		default: dprintf("?(%u)", headerType);
	}
	dprintf("\n");

	if (headerType == PCI_header_type_PCI_to_PCI_bridge) {
		uint32 primaryBus = 0, secondaryBus = 0, subordinateBus = 0;
		read_pci_config(NULL, bus, device, function, PCI_primary_bus, 1, &primaryBus);
		read_pci_config(NULL, bus, device, function, PCI_secondary_bus, 1, &secondaryBus);
		read_pci_config(NULL, bus, device, function, PCI_subordinate_bus, 1, &subordinateBus);
		dprintf("  primaryBus: %u\n", primaryBus);
		dprintf("  secondaryBus: %u\n", secondaryBus);
		dprintf("  subordinateBus: %u\n", subordinateBus);
	}

	uint32 oldValLo = 0, oldValHi = 0, sizeLo = 0, sizeHi = 0;
	uint64 val, size;
	for (int i = 0; i < ((headerType == PCI_header_type_PCI_to_PCI_bridge) ? 2 : 6); i++) {

		dprintf("  bar[%d]: ", i);

		read_pci_config(NULL, bus, device, function,
			PCI_base_registers + i*4, 4, &oldValLo);

		int regKind;
		if (oldValLo % 2 == 1) {
			regKind = kRegIo;
			dprintf("IOPORT");
		} else if (oldValLo / 2 % 4 == 0) {
			regKind = kRegMmio32;
			dprintf("MMIO32");
		} else if (oldValLo / 2 % 4 == 2) {
			regKind = kRegMmio64;
			dprintf("MMIO64");
		} else {
			dprintf("?(%d)", oldValLo / 2 % 4);
			dprintf("\n");
			continue;
		}

		read_pci_config (NULL, bus, device, function, PCI_base_registers + i*4, 4, &oldValLo);
		write_pci_config(NULL, bus, device, function, PCI_base_registers + i*4, 4, 0xffffffff);
		read_pci_config (NULL, bus, device, function, PCI_base_registers + i*4, 4, &sizeLo);
		write_pci_config(NULL, bus, device, function, PCI_base_registers + i*4, 4, oldValLo);
		val = oldValLo;
		size = sizeLo;
		if (regKind == kRegMmio64) {
			read_pci_config (NULL, bus, device, function, PCI_base_registers + (i + 1)*4, 4, &oldValHi);
			write_pci_config(NULL, bus, device, function, PCI_base_registers + (i + 1)*4, 4, 0xffffffff);
			read_pci_config (NULL, bus, device, function, PCI_base_registers + (i + 1)*4, 4, &sizeHi);
			write_pci_config(NULL, bus, device, function, PCI_base_registers + (i + 1)*4, 4, oldValHi);
			val  += ((uint64)oldValHi) << 32;
			size += ((uint64)sizeHi  ) << 32;
		} else {
			if (sizeLo != 0)
				size += ((uint64)0xffffffff) << 32;
		}
		val &= ~(uint64)0xf;
		size = ~(size & ~(uint64)0xf) + 1;
/*
		dprintf(", oldValLo: 0x%" B_PRIx32 ", sizeLo: 0x%" B_PRIx32, oldValLo,
			sizeLo);
		if (regKind == regMmio64) {
			dprintf(", oldValHi: 0x%" B_PRIx32 ", sizeHi: 0x%" B_PRIx32,
				oldValHi, sizeHi);
		}
*/
		dprintf(", adr: 0x%" B_PRIx64 ", size: 0x%" B_PRIx64, val, size);

		if (allocBars && /*val == 0 &&*/ size != 0) {
			if (regKind == kRegMmio64) {
				val = AllocRegister(regKind, size);
				write_pci_config(NULL, bus, device, function,
					PCI_base_registers + (i + 0)*4, 4, (uint32)val);
				write_pci_config(NULL, bus, device, function,
					PCI_base_registers + (i + 1)*4, 4,
					(uint32)(val >> 32));
				dprintf(" -> 0x%" B_PRIx64, val);
			} else {
				val = AllocRegister(regKind, size);
				write_pci_config(NULL, bus, device, function,
					PCI_base_registers + i*4, 4, (uint32)val);
				dprintf(" -> 0x%" B_PRIx64, val);
			}
		}

		dprintf("\n");

		if (regKind == kRegMmio64)
			i++;
	}

	// ROM
	dprintf("  rom_bar: ");
	uint32 romBaseOfs = (headerType == PCI_header_type_PCI_to_PCI_bridge) ? PCI_bridge_rom_base : PCI_rom_base;
	read_pci_config (NULL, bus, device, function, romBaseOfs, 4, &oldValLo);
	write_pci_config(NULL, bus, device, function, romBaseOfs, 4, 0xfffffffe);
	read_pci_config (NULL, bus, device, function, romBaseOfs, 4, &sizeLo);
	write_pci_config(NULL, bus, device, function, romBaseOfs, 4, oldValLo);

	val = oldValLo & PCI_rom_address_mask;
	size = ~(sizeLo & ~(uint32)0xf) + 1;
	dprintf("adr: 0x%" B_PRIx64 ", size: 0x%" B_PRIx64, val, size);
	if (allocBars && /*val == 0 &&*/ size != 0) {
		val = AllocRegister(kRegMmio32, size);
		write_pci_config(NULL, bus, device, function,
			PCI_rom_base, 4, (uint32)val);
		dprintf(" -> 0x%" B_PRIx64, val);
	}
	dprintf("\n");

	uint32 intPin = 0;
	read_pci_config(NULL, bus, device, function,
		PCI_interrupt_pin, 1, &intPin);

	InterruptMap* intMap = LookupInterruptMap(EncodePciAddress(bus, device, function), intPin);
	if (intMap == NULL)
		dprintf("no interrupt mapping for childAdr: (%d:%d:%d), childIrq: %d)\n", bus, device, function, intPin);
	else {
		write_pci_config(NULL, bus, device, function,
			PCI_interrupt_line, 1, intMap->parentIrq);
	}

	uint32 intLine = 0;
	read_pci_config(NULL, bus, device, function,
		PCI_interrupt_line, 1, &intLine);
	dprintf("  intLine: %u\n", intLine);
	dprintf("  intPin: ");
	switch (intPin) {
	case 0: dprintf("-"); break;
	case 1: dprintf("INTA#"); break;
	case 2: dprintf("INTB#"); break;
	case 3: dprintf("INTC#"); break;
	case 4: dprintf("INTD#"); break;
	default: dprintf("?(%u)", intPin); break;
	}
	dprintf("\n");
}


static void
AllocRegs()
{
	dprintf("AllocRegs()\n");
	// TODO: improve enumeration
	for (int j = 0; j < /*8*/ 1; j++) {
		for (int i = 0; i < 32; i++) {
			//dprintf("%d, %d\n", j, i);
			uint32 vendorID = 0;
			status_t res = read_pci_config(NULL, j, i, 0, PCI_vendor_id, 2, &vendorID);
			if (res >= B_OK && vendorID != 0xffff) {
				uint32 headerType = 0;
				read_pci_config(NULL, j, i, 0,
					PCI_header_type, 1, &headerType);
				if ((headerType & 0x80) != 0) {
					for (int k = 0; k < 8; k++)
						AllocRegsForDevice(j, i, k);
				} else
					AllocRegsForDevice(j, i, 0);
			}
		}
	}
}


void
pci_init0(PciInitInfo *info)
{
	sPciInitInfo = *info;
}

void
pci_init()
{
	dprintf("pci_init\n");

	sInterruptMapMask.childAdr = B_BENDIAN_TO_HOST_INT32(*((uint32*)sPciInitInfo.intMapMask + 0));
	sInterruptMapMask.childIrq = B_BENDIAN_TO_HOST_INT32(*((uint32*)sPciInitInfo.intMapMask + 3));

	sInterruptMapLen = (uint32)sPciInitInfo.intMapSize / (6 * 4);
	sInterruptMap.SetTo(new(std::nothrow) InterruptMap[sInterruptMapLen]);
	if (!sInterruptMap.IsSet())
		panic("no memory");

	for (uint32_t *it = (uint32_t*)sPciInitInfo.intMap;
		(uint8_t*)it - (uint8_t*)sPciInitInfo.intMap < sPciInitInfo.intMapSize; it += 6) {
		size_t i = (it - (uint32_t*)sPciInitInfo.intMap) / 6;

		sInterruptMap[i].childAdr = B_BENDIAN_TO_HOST_INT32(*(it + 0));
		sInterruptMap[i].childIrq = B_BENDIAN_TO_HOST_INT32(*(it + 3));
		sInterruptMap[i].parentIrqCtrl = B_BENDIAN_TO_HOST_INT32(*(it + 4));
		sInterruptMap[i].parentIrq = B_BENDIAN_TO_HOST_INT32(*(it + 5));
	}

	dprintf("  configRegs: %#" B_PRIx64 ", %#" B_PRIx64 "\n", sPciInitInfo.configRegs.start, sPciInitInfo.configRegs.size);
	dprintf("  interrupt-map:\n");
	for (size_t i = 0; i < sInterruptMapLen; i++) {
		dprintf("    ");
		// child unit address
		uint8 bus, device, function;
		DecodePciAddress(sInterruptMap[i].childAdr, bus, device, function);
		dprintf("bus: %" B_PRIu32, bus);
		dprintf(", dev: %" B_PRIu32, device);
		dprintf(", fn: %" B_PRIu32, function);

		dprintf(", childIrq: %" B_PRIu32,
			sInterruptMap[i].childIrq);
		dprintf(", parentIrq: (%" B_PRIu32,
			sInterruptMap[i].parentIrqCtrl);
		dprintf(", %" B_PRIu32, sInterruptMap[i].parentIrq);
		dprintf(")\n");
		if (i % 4 == 3 && (i + 1 < sInterruptMapLen))
			dprintf("\n");
	}

	memset(sPCIeRegisterRanges, 0, sizeof(sPCIeRegisterRanges));
	if (sPciInitInfo.ranges == NULL) {
		dprintf("  \"ranges\" property not found");
	} else {
		dprintf("  ranges:\n");
		for (uint32_t *it = (uint32_t*)sPciInitInfo.ranges;
			(uint8_t*)it - (uint8_t*)sPciInitInfo.ranges < sPciInitInfo.rangesSize; it += 7) {
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

	if (true) AllocRegs();
}
