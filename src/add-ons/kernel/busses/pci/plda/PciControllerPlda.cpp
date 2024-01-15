#include "PciControllerPlda.h"

#include <string.h>
#include <new>

#include <ByteOrder.h>
#include <KernelExport.h>

#include <dm2/device/Clock.h>
#include <dm2/device/Reset.h>
#include <dm2/device/Syscon.h>

#include <AutoDeleterDM2.h>

#include <debug.h>
#include <kernel.h>

#include "StarfivePinCtrl.h"


#define PLDA_PCI_DRIVER_MODULE_NAME "busses/pci/plda/driver/v1"


static uint32
fls(uint32 mask)
{
	if (mask == 0)
		return 0;
	uint32 pos = 1;
	while (mask != 1) {
		mask >>= 1;
		pos++;
	}
	return pos;
}


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


// #pragma mark - PciControllerPlda

status_t
PciControllerPlda::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<PciControllerPlda> driver(new(std::nothrow) PciControllerPlda(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
PciControllerPlda::Init()
{
	dprintf("PciControllerPlda::Init()\n");

	// get resources

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();
	DeviceNodePutter fdtBusNode(fFdtDevice->GetBus());
	FdtBus* fdtBus = fdtBusNode->QueryDriverInterface<FdtBus>();

	CHECK_RET(ReadResourceInfo());

	CHECK_RET(fFdtDevice->GetRegByName("reg", &fRegsPhysBase, &fRegsSize));
	dprintf("  regs: %08" B_PRIx64 ", %08" B_PRIx64 "\n", fRegsPhysBase, fRegsSize);

	CHECK_RET(fFdtDevice->GetRegByName("config", &fConfigPhysBase, &fConfigSize));
	dprintf("  config: %08" B_PRIx64 ", %08" B_PRIx64 "\n", fConfigPhysBase, fConfigSize);

	uint64 irq;
	if (!fFdtDevice->GetInterrupt(0, NULL, &irq))
		return B_ERROR;

	const void* prop;
	int propLen;
	prop = fFdtDevice->GetProp("starfive,stg-syscon", &propLen);
	if (prop == NULL || propLen < 4*(1 + 3)) {
		dprintf("  [!] no \"starfive,stg-syscon\" property\n");
		return B_ERROR;
	}

	const uint32* stgSyscon = (const uint32*)prop;

	uint32 sysconPhandle = B_BENDIAN_TO_HOST_INT32(stgSyscon[0]);
	uint32 stgArfun = B_BENDIAN_TO_HOST_INT32(stgSyscon[1]);
	uint32 stgAwfun = B_BENDIAN_TO_HOST_INT32(stgSyscon[2]);
	uint32 stgRpNep = B_BENDIAN_TO_HOST_INT32(stgSyscon[3]);

	dprintf(  "stgArfun: %#" B_PRIx32 "\n", stgArfun);
	dprintf(  "stgAwfun: %#" B_PRIx32 "\n", stgAwfun);
	dprintf(  "stgRpNep: %#" B_PRIx32 "\n", stgRpNep);

	DeviceNodePutter sysconNode(fdtBus->NodeByPhandle(sysconPhandle));
	SysconDevice* sysconDevice = sysconNode->QueryDriverInterface<SysconDevice>();
	StarfivePinCtrl pinCtrl(0x13040000, 0x10000); // !!!

#if 1
	ClockDevice* nocClk {};
	ClockDevice* tlClk {};
	ClockDevice* axiMst0Clk {};
	ClockDevice* apbClk {};

	ResetDevice* mst0Rst {};
	ResetDevice* slv0Rst {};
	ResetDevice* slvRst {};
	ResetDevice* brgRst {};
	ResetDevice* coreRst {};
	ResetDevice* apbRst {};

	CHECK_RET(fFdtDevice->GetClockByName("noc", &nocClk));
	CHECK_RET(fFdtDevice->GetClockByName("tl", &tlClk));
	CHECK_RET(fFdtDevice->GetClockByName("axi_mst0", &axiMst0Clk));
	CHECK_RET(fFdtDevice->GetClockByName("apb", &apbClk));

	CHECK_RET(fFdtDevice->GetResetByName("rst_mst0", &mst0Rst));
	CHECK_RET(fFdtDevice->GetResetByName("rst_slv0", &slv0Rst));
	CHECK_RET(fFdtDevice->GetResetByName("rst_slv", &slvRst));
	CHECK_RET(fFdtDevice->GetResetByName("rst_brg", &brgRst));
	CHECK_RET(fFdtDevice->GetResetByName("rst_core", &coreRst));
	CHECK_RET(fFdtDevice->GetResetByName("rst_apb", &apbRst));
#endif

	// init hardware

	fRegsArea.SetTo(map_physical_memory("PCI Regs MMIO", fRegsPhysBase, fRegsSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	fConfigArea.SetTo(map_physical_memory("PCI Config MMIO", fConfigPhysBase, fConfigSize,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fConfigBase));
	CHECK_RET(fConfigArea.Get());

	sysconDevice->Write4(stgRpNep, STG_SYSCON_K_RP_NEP_MASK, 1 << STG_SYSCON_K_RP_NEP_SHIFT);
	sysconDevice->Write4(stgAwfun, STG_SYSCON_CKREF_SRC_MASK, 2 << STG_SYSCON_CKREF_SRC_SHIFT);
	sysconDevice->Write4(stgAwfun, STG_SYSCON_CLKREQ_MASK, 1 << STG_SYSCON_CLKREQ_SHIFT);

	auto ShowClockResetStatus = [&]() {
		dprintf("  clock[noc]:      %d\n", nocClk->IsEnabled());
		dprintf("  clock[tl]:       %d\n", tlClk->IsEnabled());
		dprintf("  clock[axi_mst0]: %d\n", axiMst0Clk->IsEnabled());
		dprintf("  clock[apb]:      %d\n", apbClk->IsEnabled());

		dprintf("  reset[rst_mst0]: %d\n", mst0Rst->IsAsserted());
		dprintf("  reset[rst_slv0]: %d\n", slv0Rst->IsAsserted());
		dprintf("  reset[rst_slv]:  %d\n", slvRst->IsAsserted());
		dprintf("  reset[rst_brg]:  %d\n", brgRst->IsAsserted());
		dprintf("  reset[rst_core]: %d\n", coreRst->IsAsserted());
		dprintf("  reset[rst_apb]:  %d\n", apbRst->IsAsserted());
	};
	ShowClockResetStatus();

	dprintf("  init clocks and resets\n");

	ClockDevice* clock;
	for (uint32 i = 0; fFdtDevice->GetClock(i, &clock) >= B_OK; i++)
		clock->SetEnabled(true);

	ResetDevice* reset;
	for (uint32 i = 0; fFdtDevice->GetReset(i, &reset) >= B_OK; i++)
		reset->SetAsserted(false);

	ShowClockResetStatus();

	// pinctrl_select_state(dev, "perst-active");
	switch (fRegsPhysBase) {
		case 0x2B000000:
			pinCtrl.SetPinmux(26, GPOUT_LOW, GPOEN_ENABLE);
			break;
		case 0x2C000000:
			pinCtrl.SetPinmux(28, GPOUT_LOW, GPOEN_ENABLE);
			break;
	}

	for (uint32 i = 1; i < PLDA_FUNC_NUM; i++) {
		sysconDevice->Write4(stgArfun, STG_SYSCON_AXI4_SLVL_ARFUNC_MASK, (i << PLDA_PHY_FUNC_SHIFT) << STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT);
		sysconDevice->Write4(stgAwfun, STG_SYSCON_AXI4_SLVL_AWFUNC_MASK, (i << PLDA_PHY_FUNC_SHIFT) << STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT);

		fRegs->pciMisc |= PLDA_FUNCTION_DIS;
	}
	sysconDevice->Write4(stgArfun, STG_SYSCON_AXI4_SLVL_ARFUNC_MASK, 0 << STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT);
	sysconDevice->Write4(stgAwfun, STG_SYSCON_AXI4_SLVL_AWFUNC_MASK, 0 << STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT);

	fRegs->genSettings |= PLDA_RP_ENABLE;
	fRegs->pciePciIds = (IDS_PCI_TO_PCI_BRIDGE << IDS_CLASS_CODE_SHIFT) | IDS_REVISION_ID;
	fRegs->pmsgSupportRx &= ~PMSG_LTR_SUPPORT;
	fRegs->pcieWinrom |= PREF_MEM_WIN_64_SUPPORT;

	int32 atrIndex = 0;
	SetAtrEntry(atrIndex++, fConfigPhysBase, 0, 1 << 28, PciPldaAtrTrslParam{.type = PciPldaAtrTrslId::config});

	for (int32 i = 0; i < fResourceRanges.Count(); i++) {
		const resource_range& range = fResourceRanges[i];
		if (range.def.type == B_IO_MEMORY)
			SetAtrEntry(atrIndex++, range.def.host_address, range.def.pci_address, range.def.size, PciPldaAtrTrslParam{.type = PciPldaAtrTrslId::memory});
	}

	snooze(300000);
	// pinctrl_select_state(dev, "perst-default");
	switch (fRegsPhysBase) {
		case 0x2B000000:
			pinCtrl.SetPinmux(26, GPOUT_HIGH, GPOEN_ENABLE);
			break;
		case 0x2C000000:
			pinCtrl.SetPinmux(28, GPOUT_HIGH, GPOEN_ENABLE);
			break;
	}

	CHECK_RET(fIrqCtrl.Init(fRegs, irq));


	static const device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "PCI Bus Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/pci/driver/v1"}},
		{}
	};
	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fPciCtrl), attrs, NULL));

	dprintf("-PciControllerPlda::InitDriver()\n");

	return B_OK;
}


status_t
PciControllerPlda::ReadResourceInfo()
{
	const void* prop;
	int propLen;

	prop = fFdtDevice->GetProp("bus-range", &propLen);
	if (prop != NULL && propLen == 8) {
		uint32 busBeg = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 0));
		uint32 busEnd = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 1));
		dprintf("  bus-range: %" B_PRIu32 " - %" B_PRIu32 "\n", busBeg, busEnd);
	}

	prop = fFdtDevice->GetProp("interrupt-map-mask", &propLen);
	if (prop == NULL || propLen != 4 * 4) {
		dprintf("  \"interrupt-map-mask\" property not found or invalid");
		return B_ERROR;
	}
	fInterruptMapMask.childAdr = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 0));
	fInterruptMapMask.childIrq = B_BENDIAN_TO_HOST_INT32(*((uint32*)prop + 3));

	prop = fFdtDevice->GetProp("interrupt-map", &propLen);
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

	prop = fFdtDevice->GetProp("ranges", &propLen);
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

		resource_range range {
			.def = {
				.host_address = parentAdr,
				.pci_address = childAdr,
				.size = len
			},
			.free = (childAdr != 0) ? childAdr : 1
		};

		switch (type & fdtPciRangeTypeMask) {
		case fdtPciRangeIoPort:
			range.def.type = B_IO_PORT;
			break;
		case fdtPciRangeMmio32Bit:
			range.def.type = B_IO_MEMORY;
			range.def.address_type |= PCI_address_type_32;
			break;
		case fdtPciRangeMmio64Bit:
			range.def.type = B_IO_MEMORY;
			range.def.address_type |= PCI_address_type_64;
			break;
		}
		if ((type & fdtPciRangePrefechable) != 0)
			range.def.address_type |= PCI_address_prefetchable;

		if (range.def.type != 0)
			fResourceRanges.Add(range);

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


void
PciControllerPlda::SetAtrEntry(uint32 index, phys_addr_t srcAddr, phys_addr_t trslAddr,
	size_t windowSize, PciPldaAtrTrslParam trslParam)
{
	ASSERT_ALWAYS(index < B_COUNT_OF(fRegs->xr3pciAtrAxi4Slv0));
	PciPldaAtr volatile& atr = fRegs->xr3pciAtrAxi4Slv0[index];
	atr.srcAddrLow.val = PciPldaAtrAddrLow{
		.enable = 1,
		.windowSize = fls(windowSize) - 1,
		.address = (uint32)srcAddr >> 12
	}.val;
	atr.srcAddrHigh = (uint32)(srcAddr >> 32);
	atr.trslAddrLow = (uint32)trslAddr;
	atr.trslAddrHigh = (uint32)(trslAddr >> 32);
	atr.trslParam.val = trslParam.val;

	dprintf("ATR entry: 0x%010" B_PRIx64 " %s 0x%010" B_PRIx64 " [0x%010" B_PRIx64 "] (param: 0x%06x)\n",
	       srcAddr, (trslParam.dir) ? "<-" : "->",
	       trslAddr, (uint64)windowSize, trslParam.val);
}


addr_t
PciControllerPlda::ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	if ((bus == 0 || bus == 1) && !(device == 0 && function == 0))
		return 0;

	return fConfigBase + PciAddressEcam{.offset = offset, .function = function, .device = device, .bus = bus}.val;
}


// #pragma mark - PCI resource allocator

phys_addr_t
PciControllerPlda::AllocRegister(PciBarKind kind, size_t size)
{
	if ((kind.address_type & PCI_address_type_64) != 0)
		kind.address_type |= PCI_address_prefetchable;

	for (int32 i = 0; i < fResourceRanges.Count(); i++) {
		resource_range& range = fResourceRanges[i];
		if (range.def.type != kind.type || range.def.address_type != kind.address_type)
			continue;

		phys_addr_t adr = ROUNDUP(range.free, size);
		if (adr - range.def.pci_address + size > range.def.size)
			continue;

		range.free = adr + size;
		return adr;
	}

	return 0;
}


InterruptMap*
PciControllerPlda::LookupInterruptMap(uint32 childAdr, uint32 childIrq)
{
	childAdr &= fInterruptMapMask.childAdr;
	childIrq &= fInterruptMapMask.childIrq;
	for (uint32 i = 0; i < fInterruptMapLen; i++) {
		if ((fInterruptMap[i].childAdr) == childAdr
			&& (fInterruptMap[i].childIrq) == childIrq)
			return &fInterruptMap[i];
	}
	return NULL;
}


PciBarKind
PciControllerPlda::GetPciBarKind(uint32 val)
{
	if (val % 2 == 1)
		return {.type = B_IO_PORT};
	if (val / 2 % 4 == 0)
		return {.type = B_IO_MEMORY, .address_type = PCI_address_type_32};
/*
	if (val / 2 % 4 == 1)
		return kRegMmio1MB;
*/
	if (val / 2 % 4 == 2)
		return {.type = B_IO_MEMORY, .address_type = PCI_address_type_64};
	return {};
}


void
PciControllerPlda::GetBarValMask(uint32& val, uint32& mask, uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	val = 0;
	mask = 0;
	fPciCtrl.ReadPciConfig(bus, device, function, offset, 4, &val);
	fPciCtrl.WritePciConfig(bus, device, function, offset, 4, 0xffffffff);
	fPciCtrl.ReadPciConfig(bus, device, function, offset, 4, &mask);
	fPciCtrl.WritePciConfig(bus, device, function, offset, 4, val);
}


void
PciControllerPlda::GetBarKindValSize(PciBarKind& barKind, uint64& val, uint64& size, uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	uint32 oldValLo = 0, oldValHi = 0, sizeLo = 0, sizeHi = 0;
	GetBarValMask(oldValLo, sizeLo, bus, device, function, offset);
	barKind = GetPciBarKind(oldValLo);
	val = oldValLo;
	size = sizeLo;
	if (barKind.type == B_IO_MEMORY && (barKind.address_type & PCI_address_type_64) != 0) {
		GetBarValMask(oldValHi, sizeHi, bus, device, function, offset + 4);
		val  += ((uint64)oldValHi) << 32;
		size += ((uint64)sizeHi  ) << 32;
	} else {
		if (sizeLo != 0)
			size += ((uint64)0xffffffff) << 32;
	}
	if (barKind.type == B_IO_PORT)
		val &= ~(uint64)0x3;
	else
		val &= ~(uint64)0xf;
	size = ~(size & ~(uint64)0xf) + 1;
}


uint64
PciControllerPlda::GetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	uint32 oldValLo = 0, oldValHi = 0;
	fPciCtrl.ReadPciConfig(bus, device, function, offset, 4, &oldValLo);
	PciBarKind barKind = GetPciBarKind(oldValLo);
	uint64 val = oldValLo;
	if (barKind.type == B_IO_MEMORY && (barKind.address_type & PCI_address_type_64) != 0) {
		fPciCtrl.ReadPciConfig(bus, device, function, offset + 4, 4, &oldValHi);
		val += ((uint64)oldValHi) << 32;
	}
	if (barKind.type == B_IO_PORT)
		val &= ~(uint64)0x3;
	else
		val &= ~(uint64)0xf;
	return val;
}


void
PciControllerPlda::SetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset, PciBarKind barKind, uint64 val)
{
	fPciCtrl.WritePciConfig(bus, device, function, offset, 4, (uint32)val);
	if (barKind.type == B_IO_MEMORY && (barKind.address_type & PCI_address_type_64) != 0)
		fPciCtrl.WritePciConfig(bus, device, function, offset + 4, 4, (uint32)(val >> 32));
}


bool
PciControllerPlda::AllocBar(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	bool allocBars = true;

	PciBarKind regKind;
	uint64 val, size;
	GetBarKindValSize(regKind, val, size, bus, device, function, offset);
	switch (regKind.type) {
		case B_IO_PORT:   dprintf("IOPORT"); break;
		case B_IO_MEMORY: dprintf("MMIO"); break;
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

	return regKind.type == B_IO_MEMORY && (regKind.address_type & PCI_address_type_64) != 0;
}


void
PciControllerPlda::AllocRegsForDevice(uint8 bus, uint8 device, uint8 function)
{
	dprintf("AllocRegsForDevice(bus: %d, device: %d, function: %d)\n", bus, device, function);

	uint32 vendorID = 0, deviceID = 0;
	uint32 baseClass = 0, subClass = 0;
	if (fPciCtrl.ReadPciConfig(bus, device, function, PCI_vendor_id, 2, &vendorID) < B_OK || vendorID == 0xffff)
		return;

	fPciCtrl.ReadPciConfig(bus, device, function, PCI_device_id, 2, &deviceID);
	fPciCtrl.ReadPciConfig(bus, device, function, PCI_class_base, 1, &baseClass);
	fPciCtrl.ReadPciConfig(bus, device, function, PCI_class_sub, 1, &subClass);
	dprintf("  vendorID: %#04" B_PRIx32 "\n", vendorID);
	dprintf("  deviceID: %#04" B_PRIx32 "\n", deviceID);
	dprintf("  baseClass: %#04" B_PRIx32 "\n", baseClass);
	dprintf("  subClass: %#04" B_PRIx32 "\n", subClass);

	uint32 headerType = 0;
	fPciCtrl.ReadPciConfig(bus, device, function, PCI_header_type, 1, &headerType);
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
		fPciCtrl.ReadPciConfig(bus, device, function, PCI_primary_bus, 1, &primaryBus);
		fPciCtrl.ReadPciConfig(bus, device, function, PCI_secondary_bus, 1, &secondaryBus);
		fPciCtrl.ReadPciConfig(bus, device, function, PCI_subordinate_bus, 1, &subordinateBus);
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
	fPciCtrl.ReadPciConfig(bus, device, function, PCI_interrupt_pin, 1, &intPin);

	PciAddress pciAddress{
		.function = function,
		.device = device,
		.bus = bus
	};
	InterruptMap* intMap = LookupInterruptMap(pciAddress.val, intPin);
	if (intMap == NULL)
		dprintf("no interrupt mapping for childAdr: (%d:%d:%d), childIrq: %d)\n", bus, device, function, intPin);
	else
		fPciCtrl.WritePciConfig(bus, device, function, PCI_interrupt_line, 1, intMap->parentIrq);

	uint32 intLine = 0;
	fPciCtrl.ReadPciConfig(bus, device, function, PCI_interrupt_line, 1, &intLine);
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


// #pragma mark - PciControllerImpl

void*
PciControllerPlda::PciControllerImpl::QueryInterface(const char* name)
{
	if (strcmp(name, PciController::ifaceName) == 0)
		return static_cast<PciController*>(this);

	return NULL;
}


status_t
PciControllerPlda::PciControllerImpl::ReadPciConfig(uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 size, uint32* value)
{
	addr_t address = fBase.ConfigAddress(bus, device, function, offset);
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


status_t
PciControllerPlda::PciControllerImpl::WritePciConfig(uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 size, uint32 value)
{
	if (bus == 0 && device == 0 && function == 0 && offset == PCI_base_registers)
		return B_ERROR;

	addr_t address = fBase.ConfigAddress(bus, device, function, offset);
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
PciControllerPlda::PciControllerImpl::GetMaxBusDevices(int32* count)
{
	*count = 32;
	return B_OK;
}


status_t
PciControllerPlda::PciControllerImpl::ReadPciIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8* irq)
{
	return B_UNSUPPORTED;
}


status_t
PciControllerPlda::PciControllerImpl::WritePciIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}


status_t
PciControllerPlda::PciControllerImpl::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= (uint32)fBase.fResourceRanges.Count())
		return B_BAD_INDEX;

	*range = fBase.fResourceRanges[index].def;
	return B_OK;
}


status_t
PciControllerPlda::PciControllerImpl::Finalize()
{
	dprintf("PciControllerPlda::Finalize()\n");

	fBase.AllocRegsForDevice(0, 0, 0);
	fBase.AllocRegsForDevice(1, 0, 0);

	return B_OK;
}


MSIInterface*
PciControllerPlda::PciControllerImpl::GetMsiDriver()
{
	return static_cast<MSIInterface*>(&fBase.fIrqCtrl);
}


static driver_module_info sPciControllerPldaModule = {
	.info = {
		.name = PLDA_PCI_DRIVER_MODULE_NAME,
	},
	.probe = PciControllerPlda::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sPciControllerPldaModule,
	NULL
};
