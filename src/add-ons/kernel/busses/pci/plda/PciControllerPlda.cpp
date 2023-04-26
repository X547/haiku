/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "PciControllerPlda.h"
#include "Syscon.h"
#include "StarfiveClock.h"
#include "StarfiveReset.h"
#include "starfive-jh7110-clkgen.h"
#include "starfive-jh7110.h"

#include <bus/FDT.h>

#include <AutoDeleterDrivers.h>
#include <util/AutoLock.h>

#include <string.h>
#include <new>


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


//#pragma mark - driver


float
PciControllerPlda::SupportsDevice(device_node* parent)
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

	if (strcmp(compatible, "starfive,jh7110-pcie") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
PciControllerPlda::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "PLDA PCI Host Controller"} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/pci/root/driver_v1"} },
		{}
	};

	return gDeviceManager->register_node(parent, PLDA_PCI_DRIVER_MODULE_NAME, attrs, NULL,
		NULL);
}


status_t
PciControllerPlda::InitDriver(device_node* node, PciControllerPlda*& outDriver)
{
	ObjectDeleter<PciControllerPlda> driver(new(std::nothrow) PciControllerPlda());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
PciControllerPlda::ReadResourceInfo()
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
PciControllerPlda::InitDriverInt(device_node* node)
{
	fNode = node;
	dprintf("+PciControllerPlda::InitDriver()\n");

	CHECK_RET(ReadResourceInfo());

	DeviceNodePutter<&gDeviceManager> fdtNode(gDeviceManager->get_parent_node(node));

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(gDeviceManager->get_driver(fdtNode.Get(),
		(driver_module_info**)&fdtModule, (void**)&fdtDev));

	if (!fdtModule->get_reg(fdtDev, 0, &fRegsPhysBase, &fRegsSize))
		return B_ERROR;
	dprintf("  regs: %08" B_PRIx64 ", %08" B_PRIx64 "\n", fRegsPhysBase, fRegsSize);

	if (!fdtModule->get_reg(fdtDev, 1, &fConfigPhysBase, &fConfigSize))
		return B_ERROR;
	dprintf("  config: %08" B_PRIx64 ", %08" B_PRIx64 "\n", fConfigPhysBase, fConfigSize);

	uint64 irq;
	if (!fdtModule->get_interrupt(fdtDev, 0, NULL, &irq))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("PCI Regs MMIO", fRegsPhysBase, fRegsSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	fConfigArea.SetTo(map_physical_memory("PCI Config MMIO", fConfigPhysBase, fConfigSize,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fConfigBase));
	CHECK_RET(fConfigArea.Get());

	const void* prop;
	int propLen;
	prop = fdtModule->get_prop(fdtDev, "starfive,stg-syscon", &propLen);
	if (prop == NULL || propLen < 4*(1 + 4)) {
		dprintf("  [!] no \"starfive,stg-syscon\" property\n");
		return B_ERROR;
	}

	const uint32* stgSyscon = (const uint32*)prop;

	uint32 stgArfun = B_BENDIAN_TO_HOST_INT32(stgSyscon[1]);
	uint32 stgAwfun = B_BENDIAN_TO_HOST_INT32(stgSyscon[2]);
	uint32 stgRpNep = B_BENDIAN_TO_HOST_INT32(stgSyscon[3]);
	uint32 stgLnksta = B_BENDIAN_TO_HOST_INT32(stgSyscon[4]);

	dprintf(  "stgArfun: %#" B_PRIx32 "\n", stgArfun);
	dprintf(  "stgAwfun: %#" B_PRIx32 "\n", stgAwfun);
	dprintf(  "stgRpNep: %#" B_PRIx32 "\n", stgRpNep);
	dprintf(  "stgLnksta: %#" B_PRIx32 "\n", stgLnksta);

	StarfiveClock clock;
	StarfiveReset reset;
	Syscon syscon(0x10240000, 0x1000);

	syscon.SetBits(stgRpNep, STG_SYSCON_K_RP_NEP_MASK, 1 << STG_SYSCON_K_RP_NEP_SHIFT);
	syscon.SetBits(stgAwfun, STG_SYSCON_CKREF_SRC_MASK, 2 << STG_SYSCON_CKREF_SRC_SHIFT);
	syscon.SetBits(stgAwfun, STG_SYSCON_CLKREQ_MASK, 1 << STG_SYSCON_CLKREQ_SHIFT);

	switch (fRegsPhysBase) {
		case 0x2B000000:
			dprintf("  clock[JH7110_NOC_BUS_CLK_STG_AXI]: %d\n", clock.IsEnabled(JH7110_NOC_BUS_CLK_STG_AXI));
			dprintf("  clock[JH7110_PCIE0_CLK_TL]: %d\n", clock.IsEnabled(JH7110_PCIE0_CLK_TL));
			dprintf("  clock[JH7110_PCIE0_CLK_AXI_MST0]: %d\n", clock.IsEnabled(JH7110_PCIE0_CLK_AXI_MST0));
			dprintf("  clock[JH7110_PCIE0_CLK_APB]: %d\n", clock.IsEnabled(JH7110_PCIE0_CLK_APB));

			dprintf("  reset[RSTN_U0_PLDA_PCIE_AXI_MST0]: %d\n", reset.IsAsserted(RSTN_U0_PLDA_PCIE_AXI_MST0));
			dprintf("  reset[RSTN_U0_PLDA_PCIE_AXI_SLV0]: %d\n", reset.IsAsserted(RSTN_U0_PLDA_PCIE_AXI_SLV0));
			dprintf("  reset[RSTN_U0_PLDA_PCIE_AXI_SLV]: %d\n", reset.IsAsserted(RSTN_U0_PLDA_PCIE_AXI_SLV));
			dprintf("  reset[RSTN_U0_PLDA_PCIE_BRG]: %d\n", reset.IsAsserted(RSTN_U0_PLDA_PCIE_BRG));
			dprintf("  reset[RSTN_U0_PLDA_PCIE_CORE]: %d\n", reset.IsAsserted(RSTN_U0_PLDA_PCIE_CORE));
			dprintf("  reset[RSTN_U0_PLDA_PCIE_APB]: %d\n", reset.IsAsserted(RSTN_U0_PLDA_PCIE_APB));
			break;
		case 0x2C000000:
			dprintf("  clock[JH7110_NOC_BUS_CLK_STG_AXI]: %d\n", clock.IsEnabled(JH7110_NOC_BUS_CLK_STG_AXI));
			dprintf("  clock[JH7110_PCIE1_CLK_TL]: %d\n", clock.IsEnabled(JH7110_PCIE1_CLK_TL));
			dprintf("  clock[JH7110_PCIE1_CLK_AXI_MST0]: %d\n", clock.IsEnabled(JH7110_PCIE1_CLK_AXI_MST0));
			dprintf("  clock[JH7110_PCIE1_CLK_APB]: %d\n", clock.IsEnabled(JH7110_PCIE1_CLK_APB));

			dprintf("  reset[RSTN_U1_PLDA_PCIE_AXI_MST0]: %d\n", reset.IsAsserted(RSTN_U1_PLDA_PCIE_AXI_MST0));
			dprintf("  reset[RSTN_U1_PLDA_PCIE_AXI_SLV0]: %d\n", reset.IsAsserted(RSTN_U1_PLDA_PCIE_AXI_SLV0));
			dprintf("  reset[RSTN_U1_PLDA_PCIE_AXI_SLV]: %d\n", reset.IsAsserted(RSTN_U1_PLDA_PCIE_AXI_SLV));
			dprintf("  reset[RSTN_U1_PLDA_PCIE_BRG]: %d\n", reset.IsAsserted(RSTN_U1_PLDA_PCIE_BRG));
			dprintf("  reset[RSTN_U1_PLDA_PCIE_CORE]: %d\n", reset.IsAsserted(RSTN_U1_PLDA_PCIE_CORE));
			dprintf("  reset[RSTN_U1_PLDA_PCIE_APB]: %d\n", reset.IsAsserted(RSTN_U1_PLDA_PCIE_APB));
			break;
	}

	dprintf("  init clocks and resets\n");
	switch (fRegsPhysBase) {
		case 0x2B000000:
			clock.SetEnabled(JH7110_NOC_BUS_CLK_STG_AXI, true);
			clock.SetEnabled(JH7110_PCIE0_CLK_TL, true);
			clock.SetEnabled(JH7110_PCIE0_CLK_AXI_MST0, true);
			clock.SetEnabled(JH7110_PCIE0_CLK_APB, true);

			reset.SetAsserted(RSTN_U0_PLDA_PCIE_AXI_MST0, false);
			reset.SetAsserted(RSTN_U0_PLDA_PCIE_AXI_SLV0, false);
			reset.SetAsserted(RSTN_U0_PLDA_PCIE_AXI_SLV, false);
			reset.SetAsserted(RSTN_U0_PLDA_PCIE_BRG, false);
			reset.SetAsserted(RSTN_U0_PLDA_PCIE_CORE, false);
			reset.SetAsserted(RSTN_U0_PLDA_PCIE_APB, false);
			break;
		case 0x2C000000:
			clock.SetEnabled(JH7110_NOC_BUS_CLK_STG_AXI, true);
			clock.SetEnabled(JH7110_PCIE1_CLK_TL, true);
			clock.SetEnabled(JH7110_PCIE1_CLK_AXI_MST0, true);
			clock.SetEnabled(JH7110_PCIE1_CLK_APB, true);

			reset.SetAsserted(RSTN_U1_PLDA_PCIE_AXI_MST0, false);
			reset.SetAsserted(RSTN_U1_PLDA_PCIE_AXI_SLV0, false);
			reset.SetAsserted(RSTN_U1_PLDA_PCIE_AXI_SLV, false);
			reset.SetAsserted(RSTN_U1_PLDA_PCIE_BRG, false);
			reset.SetAsserted(RSTN_U1_PLDA_PCIE_CORE, false);
			reset.SetAsserted(RSTN_U1_PLDA_PCIE_APB, false);
			break;
	}

	for (uint32 i = 1; i < PLDA_FUNC_NUM; i++) {
		syscon.SetBits(stgArfun, STG_SYSCON_AXI4_SLVL_ARFUNC_MASK, (i << PLDA_PHY_FUNC_SHIFT) << STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT);
		syscon.SetBits(stgAwfun, STG_SYSCON_AXI4_SLVL_AWFUNC_MASK, (i << PLDA_PHY_FUNC_SHIFT) << STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT);

		fRegs->pciMisc |= PLDA_FUNCTION_DIS;
	}
	syscon.SetBits(stgArfun, STG_SYSCON_AXI4_SLVL_ARFUNC_MASK, 0 << STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT);
	syscon.SetBits(stgAwfun, STG_SYSCON_AXI4_SLVL_AWFUNC_MASK, 0 << STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT);

	fRegs->genSettings |= PLDA_RP_ENABLE;
	fRegs->pciePciIds = (IDS_PCI_TO_PCI_BRIDGE << IDS_CLASS_CODE_SHIFT) | IDS_REVISION_ID;
	fRegs->pmsgSupportRx &= ~PMSG_LTR_SUPPORT;
	fRegs->pcieWinrom |= PREF_MEM_WIN_64_SUPPORT;

	uint32 atrIndex = 0;
	SetAtrEntry(atrIndex++, fConfigPhysBase, 0, 1 << 28, PciPldaAtrTrslParam{.type = PciPldaAtrTrslId::config});

	for (uint32 i = 0; i < B_COUNT_OF(fResourceRanges); i++) {
		const pci_resource_range& range = fResourceRanges[i];
		if (range.type >= kPciRangeMmio && range.type < kPciRangeMmioEnd)
			SetAtrEntry(atrIndex++, range.host_addr, range.pci_addr, range.size, PciPldaAtrTrslParam{.type = PciPldaAtrTrslId::memory});
	}

	snooze(200000);

#if 1
	if (fRegsPhysBase != 0x2C000000) {
		dprintf("  skipping device\n");
		return B_ERROR;
	}
#endif

	CHECK_RET(fIrqCtrl.Init(GetRegs(), irq));

	dprintf("-PciControllerPlda::InitDriver()\n");
	return B_OK;
}


void
PciControllerPlda::UninitDriver()
{
	delete this;
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
	if (bus != 0 && bus != 1)
		return 0;

	if (device != 0 || function != 0)
		return 0;

	return fConfigBase + PciAddressEcam{.offset = offset, .function = function, .device = device, .bus = bus}.val;
}


//#pragma mark - PCI controller


status_t
PciControllerPlda::ReadConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32& value)
{
	InterruptsSpinLocker lock(fLock);

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
PciControllerPlda::WriteConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	InterruptsSpinLocker lock(fLock);

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
PciControllerPlda::GetMaxBusDevices(int32& count)
{
	count = 32;
	return B_OK;
}


status_t
PciControllerPlda::ReadIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8& irq)
{
	return B_UNSUPPORTED;
}


status_t
PciControllerPlda::WriteIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}


status_t
PciControllerPlda::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= kPciRangeEnd)
		return B_BAD_INDEX;

	*range = fResourceRanges[index];
	return B_OK;
}
