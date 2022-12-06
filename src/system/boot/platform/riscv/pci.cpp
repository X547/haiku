#include "pci.h"
#include <KernelExport.h>
#include <kernel.h>
#include <drivers/PCI.h>
#include <ByteOrder.h>
#include <AutoDeleter.h>

#include "devices.h"
#include "NvmeBlockDevice.h"

#include <string.h>
#include <new>


static PciInitInfo sPciInitInfo{};

enum PciBarKind {
	kRegIo,
	kRegMmio32,
	kRegMmio64,
	kRegMmio1MB,
	kRegUnknown,
};

union PciAddress {
	struct {
		uint32 offset: 8;
		uint32 function: 3;
		uint32 device: 5;
		uint32 bus: 8;
		uint32 unused: 8;
	};
	uint32 val;
};

union PciAddressEcam {
	struct {
		uint32 offset: 12;
		uint32 function: 3;
		uint32 device: 5;
		uint32 bus: 8;
		uint32 unused: 4;
	};
	uint32 val;
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


static inline addr_t
PciConfigAdr(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	PciAddressEcam address {
		.offset = offset,
		.function = function,
		.device = device,
		.bus = bus
	};
	PciAddressEcam addressEnd = address;
	addressEnd.offset = /*~(uint32)0*/ 4095;

	if (addressEnd.val >= sPciInitInfo.configRegs.size)
		return 0;

	return sPciInitInfo.configRegs.start + address.val;
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
read_pci_config(void* cookie, uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32* value)
{
	addr_t address = PciConfigAdr(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: *value = ReadReg8(address); break;
		case 2: *value = ReadReg16(address); break;
		case 4: *value = *(vuint32*)address; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


static status_t
write_pci_config(void* cookie, uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	addr_t address = PciConfigAdr(bus, device, function, offset);
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


static PciBarKind
GetPciBarKind(uint32 val)
{
	if (val % 2 == 1)
		return kRegIo;
	if (val / 2 % 4 == 0)
		return kRegMmio32;
	if (val / 2 % 4 == 1)
		return kRegMmio1MB;
	if (val / 2 % 4 == 2)
		return kRegMmio64;
	return kRegUnknown;
}


static void
GetBarValMask(uint32& val, uint32& mask, uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	val = 0;
	mask = 0;
	read_pci_config (NULL, bus, device, function, offset, 4, &val);
	write_pci_config(NULL, bus, device, function, offset, 4, 0xffffffff);
	read_pci_config (NULL, bus, device, function, offset, 4, &mask);
	write_pci_config(NULL, bus, device, function, offset, 4, val);
}


static void
GetBarKindValSize(PciBarKind& barKind, uint64& val, uint64& size, uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	uint32 oldValLo = 0, oldValHi = 0, sizeLo = 0, sizeHi = 0;
	GetBarValMask(oldValLo, sizeLo, bus, device, function, offset);
	barKind = GetPciBarKind(oldValLo);
	val = oldValLo;
	size = sizeLo;
	if (barKind == kRegMmio64) {
		GetBarValMask(oldValHi, sizeHi, bus, device, function, offset + 4);
		val  += ((uint64)oldValHi) << 32;
		size += ((uint64)sizeHi  ) << 32;
	} else {
		if (sizeLo != 0)
			size += ((uint64)0xffffffff) << 32;
	}
	if (barKind == kRegIo)
		val &= ~(uint64)0x3;
	else
		val &= ~(uint64)0xf;
	size = ~(size & ~(uint64)0xf) + 1;
}


static uint64
GetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	uint32 oldValLo = 0, oldValHi = 0;
	read_pci_config(NULL, bus, device, function, offset, 4, &oldValLo);
	PciBarKind barKind = GetPciBarKind(oldValLo);
	uint64 val = oldValLo;
	if (barKind == kRegMmio64) {
		read_pci_config(NULL, bus, device, function, offset + 5, 4, &oldValHi);
		val += ((uint64)oldValHi) << 32;
	}
	if (barKind == kRegIo)
		val &= ~(uint64)0x3;
	else
		val &= ~(uint64)0xf;
	return val;
}


static void
SetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset, PciBarKind barKind, uint64 val)
{
	write_pci_config(NULL, bus, device, function, offset, 4, (uint32)val);
	if (barKind == kRegMmio64)
		write_pci_config(NULL, bus, device, function, offset + 4, 4, (uint32)(val >> 32));
}


static bool
AllocBar(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	bool allocBars = true;

	PciBarKind regKind;
	uint64 val, size;
	GetBarKindValSize(regKind, val, size, bus, device, function, offset);
	switch (regKind) {
		case kRegIo:     dprintf("IOPORT"); break;
		case kRegMmio32: dprintf("MMIO32"); break;
		case kRegMmio64: dprintf("MMIO64"); break;
		default:
			dprintf("?(%#x)", (unsigned)(val%16));
			dprintf("\n");
			return false;
	}

	dprintf(", adr: 0x%" B_PRIx64 ", size: 0x%" B_PRIx64, val, size);

	if (allocBars && size != 0) {
		val = AllocRegister(regKind, size);
		SetBarVal(bus, device, function, offset, regKind, val);
		dprintf(" -> 0x%" B_PRIx64, val);
	}

	dprintf("\n");

	return regKind == kRegMmio64;
}


static void
AllocRegsForDevice(uint8 bus, uint8 device, uint8 function)
{
	dprintf("AllocRegsForDevice(bus: %d, device: %d, function: %d)\n", bus, device, function);

	uint32 vendorID = 0, deviceID = 0;
	read_pci_config(NULL, bus, device, function, PCI_vendor_id, 2, &vendorID);
	read_pci_config(NULL, bus, device, function, PCI_device_id, 2, &deviceID);
	dprintf("  vendorID: %#04" B_PRIx32 "\n", vendorID);
	dprintf("  deviceID: %#04" B_PRIx32 "\n", deviceID);

	uint32 headerType = 0;
	read_pci_config(NULL, bus, device, function, PCI_header_type, 1, &headerType);
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

	for (int i = 0; i < ((headerType == PCI_header_type_PCI_to_PCI_bridge) ? 2 : 6); i++) {
		dprintf("  bar[%d]: ", i);
		if (AllocBar(bus, device, function, PCI_base_registers + i*4))
			i++;
	}
	// ROM
	dprintf("  romBar: ");
	uint32 romBaseOfs = (headerType == PCI_header_type_PCI_to_PCI_bridge) ? PCI_bridge_rom_base : PCI_rom_base;
	AllocBar(bus, device, function, romBaseOfs);

	uint32 intPin = 0;
	read_pci_config(NULL, bus, device, function, PCI_interrupt_pin, 1, &intPin);

	PciAddress pciAddress{
		.function = function,
		.device = device,
		.bus = bus
	};
	InterruptMap* intMap = LookupInterruptMap(pciAddress.val, intPin);
	if (intMap == NULL)
		dprintf("no interrupt mapping for childAdr: (%d:%d:%d), childIrq: %d)\n", bus, device, function, intPin);
	else
		write_pci_config(NULL, bus, device, function, PCI_interrupt_line, 1, intMap->parentIrq);

	uint32 intLine = 0;
	read_pci_config(NULL, bus, device, function, PCI_interrupt_line, 1, &intLine);
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
PciForEachDevice(bool (*handler)(void* arg, uint8 bus, uint8 device, uint8 function), void* arg)
{
	// TODO: improve enumeration
	for (int j = 0; j < /*8*/ 1; j++) {
		for (int i = 0; i < 32; i++) {
			uint32 vendorID = 0;
			status_t res = read_pci_config(NULL, j, i, 0, PCI_vendor_id, 2, &vendorID);
			if (res >= B_OK && vendorID != 0xffff) {
				uint32 headerType = 0;
				read_pci_config(NULL, j, i, 0, PCI_header_type, 1, &headerType);
				if ((headerType & 0x80) != 0) {
					for (int k = 0; k < 8; k++)
						if (!handler(arg, j, i, k)) return;
				} else
					if (!handler(arg, j, i, 0)) return;
			}
		}
	}
}


static void
AllocRegs()
{
	dprintf("AllocRegs()\n");
	PciForEachDevice([](void* arg, uint8 bus, uint8 device, uint8 function) {
		AllocRegsForDevice(bus, device, function);
		return true;
	}, NULL);
}


static void
PciLookupDrivers()
{
	dprintf("PciLookupDrivers()\n");
	PciForEachDevice([](void* arg, uint8 bus, uint8 device, uint8 function) {
		uint32 baseClass = 0, subClass = 0;
		read_pci_config(NULL, bus, device, function, PCI_class_base, 1, &baseClass);
		read_pci_config(NULL, bus, device, function, PCI_class_sub, 1, &subClass);
		if (baseClass == PCI_mass_storage && subClass == PCI_nvm) {
			void* regs = (void*)(addr_t)GetBarVal(bus, device, function, PCI_base_registers);
			ObjectDeleter<NvmeBlockDevice> device(CreateNvmeBlockDev(regs));
			if (!device.IsSet())
				return true;

			if (platform_add_device(device.Get()) >= B_OK)
				device.Detach();
		}
		return true;
	}, NULL);
}


void
pci_init0(PciInitInfo* info)
{
	sPciInitInfo = *info;
}


void
pci_init()
{
	dprintf("pci_init\n");
	if (sPciInitInfo.configRegs.size == 0)
		return;

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
		PciAddress pciAddress{.val = sInterruptMap[i].childAdr};
		dprintf("bus: %" B_PRIu32, pciAddress.bus);
		dprintf(", dev: %" B_PRIu32, pciAddress.device);
		dprintf(", fn: %" B_PRIu32, pciAddress.function);

		dprintf(", childIrq: %" B_PRIu32, sInterruptMap[i].childIrq);
		dprintf(", parentIrq: (%" B_PRIu32, sInterruptMap[i].parentIrqCtrl);
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
	PciLookupDrivers();
}
