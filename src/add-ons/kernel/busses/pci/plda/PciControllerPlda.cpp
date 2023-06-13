/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "PciControllerPlda.h"
#include "Syscon.h"
#include "StarfiveClock.h"
#include "StarfiveReset.h"
#include "StarfivePinCtrl.h"
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
			fResourceFree[outType] = (childAdr != 0) ? childAdr : 1;
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


static int32
FdtFindString(const char* prop, int size, const char* name)
{
	int32 index = 0;
	int nameLen = strlen(name);
	const char* propEnd = prop + size;
	while (propEnd - prop > 0) {
		int curLen = strlen(prop);
		if (curLen == nameLen && memcmp(prop, name, curLen + 1) == 0)
			return index;
		prop += curLen + 1;
		index++;
	}
	return B_ERROR;
}


static int32
FdtGetClock(fdt_device_module_info* fdtModule, fdt_device* fdtDev, const char* name)
{
	const void* prop;
	int propLen;
	prop = fdtModule->get_prop(fdtDev, "clock-names", &propLen);
	if (prop == NULL) {
		dprintf("  [!] no \"clock-names\" property\n");
		return B_ERROR;
	}
	int32 index = FdtFindString((const char*)prop, propLen, name);
	if (index < 0) {
		dprintf("  [!] clock \"%s\" not found\n", name);
		return B_ERROR;
	}
	CHECK_RET(index);
	prop = fdtModule->get_prop(fdtDev, "clocks", &propLen);
	if (prop == NULL || propLen < 4*(2*(index + 1))) {
		dprintf("  [!] no \"clocks\" property\n");
		return B_ERROR;
	}
	const uint32* clocks = (const uint32*)prop;
	int32 clockId = B_BENDIAN_TO_HOST_INT32(clocks[2*index + 1]);
	return clockId;
}


static int32
FdtGetReset(fdt_device_module_info* fdtModule, fdt_device* fdtDev, const char* name)
{
	const void* prop;
	int propLen;
	prop = fdtModule->get_prop(fdtDev, "reset-names", &propLen);
	if (prop == NULL) {
		dprintf("  [!] no \"reset-names\" property\n");
		return B_ERROR;
	}
	int32 index = FdtFindString((const char*)prop, propLen, name);
	if (index < 0) {
		dprintf("  [!] reset \"%s\" not found\n", name);
		return B_ERROR;
	}
	prop = fdtModule->get_prop(fdtDev, "resets", &propLen);
	if (prop == NULL || propLen < 4*(2*(index + 1))) {
		dprintf("  [!] no \"resets\" property\n");
		return B_ERROR;
	}
	const uint32* resets = (const uint32*)prop;
	int32 resetId = B_BENDIAN_TO_HOST_INT32(resets[2*index + 1]);
	return resetId;
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
	if (prop == NULL || propLen < 4*(1 + 3)) {
		dprintf("  [!] no \"starfive,stg-syscon\" property\n");
		return B_ERROR;
	}

	const uint32* stgSyscon = (const uint32*)prop;

	uint32 stgArfun = B_BENDIAN_TO_HOST_INT32(stgSyscon[1]);
	uint32 stgAwfun = B_BENDIAN_TO_HOST_INT32(stgSyscon[2]);
	uint32 stgRpNep = B_BENDIAN_TO_HOST_INT32(stgSyscon[3]);

	dprintf(  "stgArfun: %#" B_PRIx32 "\n", stgArfun);
	dprintf(  "stgAwfun: %#" B_PRIx32 "\n", stgAwfun);
	dprintf(  "stgRpNep: %#" B_PRIx32 "\n", stgRpNep);

	StarfiveClock clock;
	StarfiveReset reset;
	Syscon syscon(0x10240000, 0x1000);
	StarfivePinCtrl pinCtrl(0x13040000, 0x10000);

	syscon.SetBits(stgRpNep, STG_SYSCON_K_RP_NEP_MASK, 1 << STG_SYSCON_K_RP_NEP_SHIFT);
	syscon.SetBits(stgAwfun, STG_SYSCON_CKREF_SRC_MASK, 2 << STG_SYSCON_CKREF_SRC_SHIFT);
	syscon.SetBits(stgAwfun, STG_SYSCON_CLKREQ_MASK, 1 << STG_SYSCON_CLKREQ_SHIFT);

	int32 nocClk     = FdtGetClock(fdtModule, fdtDev, "noc");
	int32 tlClk      = FdtGetClock(fdtModule, fdtDev, "tl");
	int32 axiMst0Clk = FdtGetClock(fdtModule, fdtDev, "axi_mst0");
	int32 apbClk     = FdtGetClock(fdtModule, fdtDev, "apb");
	CHECK_RET(nocClk);
	CHECK_RET(tlClk);
	CHECK_RET(axiMst0Clk);
	CHECK_RET(apbClk);

	int32 mst0Rst = FdtGetReset(fdtModule, fdtDev, "rst_mst0");
	int32 slv0Rst = FdtGetReset(fdtModule, fdtDev, "rst_slv0");
	int32 slvRst  = FdtGetReset(fdtModule, fdtDev, "rst_slv");
	int32 brgRst  = FdtGetReset(fdtModule, fdtDev, "rst_brg");
	int32 coreRst = FdtGetReset(fdtModule, fdtDev, "rst_core");
	int32 apbRst  = FdtGetReset(fdtModule, fdtDev, "rst_apb");
	CHECK_RET(mst0Rst);
	CHECK_RET(slv0Rst);
	CHECK_RET(slvRst);
	CHECK_RET(brgRst);
	CHECK_RET(coreRst);
	CHECK_RET(apbRst);

	auto ShowClockResetStatus = [&]() {
		dprintf("  clock[noc]:      %d\n", clock.IsEnabled(nocClk));
		dprintf("  clock[tl]:       %d\n", clock.IsEnabled(tlClk));
		dprintf("  clock[axi_mst0]: %d\n", clock.IsEnabled(axiMst0Clk));
		dprintf("  clock[apb]:      %d\n", clock.IsEnabled(apbClk));

		dprintf("  reset[rst_mst0]: %d\n", reset.IsAsserted(mst0Rst));
		dprintf("  reset[rst_slv0]: %d\n", reset.IsAsserted(slv0Rst));
		dprintf("  reset[rst_slv]:  %d\n", reset.IsAsserted(slvRst));
		dprintf("  reset[rst_brg]:  %d\n", reset.IsAsserted(brgRst));
		dprintf("  reset[rst_core]: %d\n", reset.IsAsserted(coreRst));
		dprintf("  reset[rst_apb]:  %d\n", reset.IsAsserted(apbRst));
	};
	ShowClockResetStatus();

	dprintf("  init clocks and resets\n");
	clock.SetEnabled(nocClk,     true);
	clock.SetEnabled(tlClk,      true);
	clock.SetEnabled(axiMst0Clk, true);
	clock.SetEnabled(apbClk,     true);

	reset.SetAsserted(mst0Rst, false);
	reset.SetAsserted(slv0Rst, false);
	reset.SetAsserted(slvRst,  false);
	reset.SetAsserted(brgRst,  false);
	reset.SetAsserted(coreRst, false);
	reset.SetAsserted(apbRst,  false);

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
	if ((bus == 0 || bus == 1) && !(device == 0 && function == 0))
		return 0;

	return fConfigBase + PciAddressEcam{.offset = offset, .function = function, .device = device, .bus = bus}.val;
}


//#pragma mark - PCI controller


status_t
PciControllerPlda::ReadConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32& value)
{
	InterruptsSpinLocker lock(fLock);
	// dprintf("PciControllerPlda::ReadConfig(%u, %u, %u, %u, %u)\n", bus, device, function, offset, size);

	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0) {
		// dprintf("  address: 0\n");
		return B_ERROR;
	}

	switch (size) {
		case 1: value = ReadReg8(address); break;
		case 2: value = ReadReg16(address); break;
		case 4: value = *(vuint32*)address; break;
		default:
			return B_ERROR;
	}
	// dprintf("  value: %#" B_PRIx32 "\n", value);

	return B_OK;
}


status_t
PciControllerPlda::WriteConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	InterruptsSpinLocker lock(fLock);
	// dprintf("PciControllerPlda::WriteConfig(%u, %u, %u, %u, %u, %#" B_PRIx32 ")\n", bus, device, function, offset, size, value);

	if (bus == 0 && device == 0 && function == 0 && offset == PCI_base_registers)
		return B_ERROR;

	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0) {
		// dprintf("  address: 0\n");
		return B_ERROR;
	}

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


phys_addr_t
PciControllerPlda::AllocRegister(uint32 kind, size_t size)
{
	if (kind == kPciRangeMmio + kPciRangeMmio64Bit) {
		kind += kPciRangeMmioPrefetch;
	}

	auto& range = fResourceRanges[kind];

	phys_addr_t adr = ROUNDUP(fResourceFree[kind], size);
	if (adr - range.pci_addr + size > range.size)
		return 0;

	fResourceFree[kind] = adr + size;

	return adr;
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


uint32
PciControllerPlda::GetPciBarKind(uint32 val)
{
	if (val % 2 == 1)
		return kPciRangeIoPort;
	if (val / 2 % 4 == 0)
		return kPciRangeMmio;
/*
	if (val / 2 % 4 == 1)
		return kRegMmio1MB;
*/
	if (val / 2 % 4 == 2)
		return kPciRangeMmio + kPciRangeMmio64Bit;
	return kPciRangeInvalid;
}


void
PciControllerPlda::GetBarValMask(uint32& val, uint32& mask, uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	val = 0;
	mask = 0;
	ReadConfig(bus, device, function, offset, 4, val);
	WriteConfig(bus, device, function, offset, 4, 0xffffffff);
	ReadConfig(bus, device, function, offset, 4, mask);
	WriteConfig(bus, device, function, offset, 4, val);
}


void
PciControllerPlda::GetBarKindValSize(uint32& barKind, uint64& val, uint64& size, uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	uint32 oldValLo = 0, oldValHi = 0, sizeLo = 0, sizeHi = 0;
	GetBarValMask(oldValLo, sizeLo, bus, device, function, offset);
	barKind = GetPciBarKind(oldValLo);
	val = oldValLo;
	size = sizeLo;
	if (barKind == kPciRangeMmio + kPciRangeMmio64Bit) {
		GetBarValMask(oldValHi, sizeHi, bus, device, function, offset + 4);
		val  += ((uint64)oldValHi) << 32;
		size += ((uint64)sizeHi  ) << 32;
	} else {
		if (sizeLo != 0)
			size += ((uint64)0xffffffff) << 32;
	}
	if (barKind == kPciRangeIoPort)
		val &= ~(uint64)0x3;
	else
		val &= ~(uint64)0xf;
	size = ~(size & ~(uint64)0xf) + 1;
}


uint64
PciControllerPlda::GetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	uint32 oldValLo = 0, oldValHi = 0;
	ReadConfig(bus, device, function, offset, 4, oldValLo);
	uint32 barKind = GetPciBarKind(oldValLo);
	uint64 val = oldValLo;
	if (barKind == kPciRangeMmio + kPciRangeMmio64Bit) {
		ReadConfig(bus, device, function, offset + 4, 4, oldValHi);
		val += ((uint64)oldValHi) << 32;
	}
	if (barKind == kPciRangeIoPort)
		val &= ~(uint64)0x3;
	else
		val &= ~(uint64)0xf;
	return val;
}


void
PciControllerPlda::SetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 barKind, uint64 val)
{
	WriteConfig(bus, device, function, offset, 4, (uint32)val);
	if (barKind == kPciRangeMmio + kPciRangeMmio64Bit)
		WriteConfig(bus, device, function, offset + 4, 4, (uint32)(val >> 32));
}


bool
PciControllerPlda::AllocBar(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	bool allocBars = true;

	uint32 regKind;
	uint64 val, size;
	GetBarKindValSize(regKind, val, size, bus, device, function, offset);
	switch (regKind) {
		case kPciRangeIoPort:                    dprintf("IOPORT"); break;
		case kPciRangeMmio:                      dprintf("MMIO32"); break;
		case kPciRangeMmio + kPciRangeMmio64Bit: dprintf("MMIO64"); break;
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

	return regKind == kPciRangeMmio + kPciRangeMmio64Bit;
}


void
PciControllerPlda::AllocRegsForDevice(uint8 bus, uint8 device, uint8 function)
{
	dprintf("AllocRegsForDevice(bus: %d, device: %d, function: %d)\n", bus, device, function);

	uint32 vendorID = 0, deviceID = 0;
	uint32 baseClass = 0, subClass = 0;
	if (ReadConfig(bus, device, function, PCI_vendor_id, 2, vendorID) < B_OK || vendorID == 0xffff)
		return;

	ReadConfig(bus, device, function, PCI_device_id, 2, deviceID);
	ReadConfig(bus, device, function, PCI_class_base, 1, baseClass);
	ReadConfig(bus, device, function, PCI_class_sub, 1, subClass);
	dprintf("  vendorID: %#04" B_PRIx32 "\n", vendorID);
	dprintf("  deviceID: %#04" B_PRIx32 "\n", deviceID);
	dprintf("  baseClass: %#04" B_PRIx32 "\n", baseClass);
	dprintf("  subClass: %#04" B_PRIx32 "\n", subClass);

	uint32 headerType = 0;
	ReadConfig(bus, device, function, PCI_header_type, 1, headerType);
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
		ReadConfig(bus, device, function, PCI_primary_bus, 1, primaryBus);
		ReadConfig(bus, device, function, PCI_secondary_bus, 1, secondaryBus);
		ReadConfig(bus, device, function, PCI_subordinate_bus, 1, subordinateBus);
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
	ReadConfig(bus, device, function, PCI_interrupt_pin, 1, intPin);

	PciAddress pciAddress{
		.function = function,
		.device = device,
		.bus = bus
	};
	InterruptMap* intMap = LookupInterruptMap(pciAddress.val, intPin);
	if (intMap == NULL)
		dprintf("no interrupt mapping for childAdr: (%d:%d:%d), childIrq: %d)\n", bus, device, function, intPin);
	else
		WriteConfig(bus, device, function, PCI_interrupt_line, 1, intMap->parentIrq);

	uint32 intLine = 0;
	ReadConfig(bus, device, function, PCI_interrupt_line, 1, intLine);
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


status_t
PciControllerPlda::Finalize()
{
	dprintf("PciControllerPlda::Finalize()\n");

	AllocRegsForDevice(0, 0, 0);
	AllocRegsForDevice(1, 0, 0);

	return B_OK;
}
