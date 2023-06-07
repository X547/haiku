#include "DwmacDriver.h"
#include "DwmacNetDevice.h"
#include "kernel_interface.h"

#include <bus/FDT.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <util/AutoLock.h>

#include <stdio.h>
#include <string.h>

#include <new>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


DwmacRoster DwmacRoster::sInstance;


float
DwmacDriver::SupportsDevice(device_node* parent)
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

	if (strcmp(compatible, "snps,dwmac-5.10a") != 0
		&& strcmp(compatible, "starfive,dwmac") != 0
		&& strcmp(compatible, "starfive,jh7110-eqos-5.20") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
DwmacDriver::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Designware MAC"}},
		{}
	};

	return gDeviceManager->register_node(parent, DWMAC_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


status_t
DwmacDriver::InitDriver(device_node* node, DwmacDriver*& outDriver)
{
	ObjectDeleter<DwmacDriver> driver(new(std::nothrow) DwmacDriver());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
DwmacDriver::InitDriverInt(device_node* node)
{
	dprintf("DwmacDriver::InitDriverInt()\n");
	fNode = node;

	DeviceNodePutter<&gDeviceManager> fdtNode(gDeviceManager->get_parent_node(node));

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(gDeviceManager->get_driver(fdtNode.Get(),
		(driver_module_info**)&fdtModule, (void**)&fdtDev));

	addr_t regsPhysBase;
	size_t regsSize;
	if (!fdtModule->get_reg(fdtDev, 0, &regsPhysBase, &regsSize))
		return B_ERROR;
	dprintf("  regs: %08" B_PRIx64 ", %08" B_PRIx64 "\n", regsPhysBase, regsSize);

	fRegsArea.SetTo(map_physical_memory("DWMAC Regs MMIO", regsPhysBase, regsSize, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	RecursiveLocker locker(DwmacRoster::Instance().Lock());

	fId = gDeviceManager->create_id(DWMAC_DEVICE_ID_GENERATOR);
	CHECK_RET(fId);

	DwmacRoster::Instance().Insert(this);

	dprintf("-DwmacDriver::InitDriverInt()\n");
	return B_OK;
}


void
DwmacDriver::UninitDriver()
{
	if (fNetDevice != NULL) {
		fNetDevice->ReleaseDriver();
		fNetDevice = NULL;
	}

	RecursiveLocker locker(DwmacRoster::Instance().Lock());
	DwmacRoster::Instance().Remove(this);

	gDeviceManager->free_id(DWMAC_DEVICE_ID_GENERATOR, fId);
	fId = -1;

	delete this;
}


status_t
DwmacDriver::RegisterChildDevices()
{
	dprintf("DwmacDriver::RegisterChildDevices()\n");
	char name[64];
	snprintf(name, sizeof(name), "net/dwmac/%" B_PRId32, fId);
	dprintf("  name: \"%s\"\n", name);

	CHECK_RET(gDeviceManager->publish_device(fNode, name, DWMAC_DEVICE_MODULE_NAME));

	return B_OK;
}


status_t
DwmacDriver::Start()
{
	//StartClocks();
	//StartResets();
	snooze(10);



	return B_ERROR;
}


status_t
DwmacDriver::Stop()
{
	return B_ERROR;
}


status_t
DwmacDriver::MdioWaitIdle()
{
	for (uint32 i = 0; i < 1000000; i++) {
		if (!fRegs->mac.mdioAddr.gb)
			return B_OK;

		snooze(1);
	}
	return B_TIMED_OUT;
}


status_t
DwmacDriver::MdioRead(uint32 addr, uint32 reg, uint32& value)
{
	CHECK_RET(MdioWaitIdle());

	DwmacMdioAddr mdioAddr {.val = fRegs->mac.mdioAddr.val};
	mdioAddr.val &= DwmacMdioAddr{.c45e = true, .skap = true}.val;
	mdioAddr.pa  = addr;
	mdioAddr.rda = reg;
	mdioAddr.cr  = DwmacMdioAddrCr::cr250_300;
	mdioAddr.goc = DwmacMdioAddrGoc::read;
	mdioAddr.gb  = true;
	fRegs->mac.mdioAddr.val = mdioAddr.val;

	snooze(10);

	CHECK_RET(MdioWaitIdle());

	value = fRegs->mac.mdioData.gd;
	return B_OK;
}


status_t
DwmacDriver::MdioWrite(uint32 addr, uint32 reg, uint32 value)
{
	CHECK_RET(MdioWaitIdle());

	fRegs->mac.mdioData.val = value;

	DwmacMdioAddr mdioAddr {.val = fRegs->mac.mdioAddr.val};
	mdioAddr.val &= DwmacMdioAddr{.c45e = true, .skap = true}.val;
	mdioAddr.pa  = addr;
	mdioAddr.rda = reg;
	mdioAddr.cr  = DwmacMdioAddrCr::cr250_300;
	mdioAddr.goc = DwmacMdioAddrGoc::write;
	mdioAddr.gb  = true;
	fRegs->mac.mdioAddr.val = mdioAddr.val;

	snooze(10);

	CHECK_RET(MdioWaitIdle());

	return B_OK;
}


status_t
DwmacDriver::InitDma()
{
	fTxDescCnt = 16;
	fRxDescCnt = 16;

	size_t dmaAreaSize = 0;
	size_t descsOfs = dmaAreaSize;
	dmaAreaSize += fTxDescCnt*sizeof(DwmacDesc);
	dmaAreaSize += fRxDescCnt*sizeof(DwmacDesc);
	dmaAreaSize = ROUNDUP(dmaAreaSize, dmaMinAlign);
	size_t buffersOfs = dmaAreaSize;
	dmaAreaSize += fTxDescCnt*dwmacMaxPacketSize;
	dmaAreaSize += fRxDescCnt*dwmacMaxPacketSize;
	dmaAreaSize = ROUNDUP(dmaAreaSize, B_PAGE_SIZE);

	fDmaArea.SetTo(create_area(
		"DWMAC DMA",
		&fDmaAdr,
		B_ANY_ADDRESS,
		dmaAreaSize,
		B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));
	CHECK_RET(fDmaArea.Get());

	physical_entry pe;
	CHECK_RET(get_memory_map(fDmaAdr, dmaAreaSize, &pe, 1));
	fDmaPhysAdr = pe.address;

	fDescs   = (DwmacDesc*)((uint8*)fDmaAdr + descsOfs);
	fBuffers =              (uint8*)fDmaAdr + buffersOfs;

	return B_OK;
}


status_t
DwmacDriver::Send(phys_addr_t data, size_t size)
{
	uint32 descIdx = fTxDescIdx;
	DwmacDesc* desc = GetTxDesc(fTxDescIdx);
	fTxDescIdx = (fTxDescIdx + 1) % fTxDescCnt;

	desc->des0 = data;
	desc->des1 = 0;
	desc->des2 = size;
	memory_full_barrier();
	desc->des3.val = DwmacDescDes3{
		.length = (uint32)size,
		.ld = true,
		.fd = true,
		.own = true
	}.val;

	fRegs->dma.channels[0].txEndAddr = ToPhysDmaAdr(GetTxDesc(fTxDescIdx));

	return descIdx;
}


status_t
DwmacDriver::Recv(phys_addr_t data, size_t size)
{
	uint32 descIdx = fRxDescIdx;
	DwmacDesc* desc = GetTxDesc(fRxDescIdx);

	desc->des0 = 0;
	memory_full_barrier();
	desc->des0 = data;
	desc->des1 = 0;
	desc->des2 = 0;
	memory_full_barrier();
	desc->des3.val = DwmacDescDes3{
		.buf1v = true,
		.own = true
	}.val;
	fRegs->dma.channels[0].rxEndAddr = ToPhysDmaAdr(desc);
	fRxDescIdx = (fRxDescIdx + 1) % fRxDescCnt;

	return descIdx;
}
