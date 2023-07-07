#include "DwmacDriver.h"
#include "DwmacNetDevice.h"
#include "kernel_interface.h"
#include "StarfiveClock.h"
#include "StarfiveReset.h"

#include <bus/FDT.h>

#include <AutoDeleter.h>
#include <AutoDeleterDrivers.h>
#include <util/AutoLock.h>

#include <stdio.h>
#include <string.h>

#include <new>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


DwmacRoster DwmacRoster::sInstance;


template<typename Cond> static status_t
WaitForCond(Cond cond, int32 attempts, bigtime_t retryInterval)
{
	for (; attempts > 0; attempts--) {
		if (cond())
			return B_OK;

		snooze(retryInterval);
	}
	return B_TIMED_OUT;
}


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
	StarfiveClock clock;

	CHECK_RET(StartClocks());
	CHECK_RET(StartResets());
	snooze(10);

	CHECK_RET(WaitForCond([&](){return !fRegs->dma.busMode.swr;}, 50000, 1));

	uint64 rate = clock.GetRate(fClkTx);
	fRegs->mac.usTicCounter = (rate / 1000000) - 1;

	// TODO: init PHY

	// CHECK_RET(AdjustLink());

	DwmacMtlTxOpMode txOpMode = {.val = fRegs->mtl.chan[0].txOpMode.val};
	txOpMode.tsf = true;
	txOpMode.txqen = DwmacMtlTxOpModeTxqen::enabled;
	fRegs->mtl.chan[0].txOpMode.val = txOpMode.val;

	fRegs->mtl.chan[0].txqWeight = 0x10;
	fRegs->mtl.chan[0].rxOpMode.rsf = true;

	DwmacHwFeature1 macHwFeature1 {.val = fRegs->mac.hwFeature1.val};
	uint32 tqs = (128 << macHwFeature1.txFifoSize) / 256 - 1;
	uint32 rqs = (128 << macHwFeature1.rxFifoSize) / 256 - 1;

	fRegs->mtl.chan[0].txOpMode.tqs = tqs;
	fRegs->mtl.chan[0].rxOpMode.rqs = rqs;

	if (rqs >= ((4096 / 256) - 1)) {
		// TODO
	}

	fRegs->mac.rxqCtrl0.rxq0en = DwmacRxqCtrl0Rxq0en::enabledDcb;
	fRegs->mac.rxqCtrl1 = 0x00100000;
	fRegs->mac.packetFilter = 0x1;
	fRegs->mac.qxTxFlowCtrl[0].pt = 0xffff;

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
	return WaitForCond([&](){return !fRegs->mac.mdioAddr.gb;}, 1000000, 1);
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
DwmacDriver::StartClocks()
{
	return B_OK;
}


status_t
DwmacDriver::StartResets()
{
	return B_OK;
}


status_t
DwmacDriver::InitDma()
{
	fTxDescCnt = 64;
	fRxDescCnt = 64;

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
DwmacDriver::Send(generic_io_vec* vector, size_t vectorCount)
{
	uint32 descIdx = fTxDescIdx;

	size_t totalLen = 0;
	for (size_t i = 0; i < vectorCount; i++)
		totalLen += vector[i].length;

	for (size_t i = 0; i < vectorCount; i++) {
		DwmacDesc* desc = GetTxDesc(fTxDescIdx);
		fTxDescIdx = (fTxDescIdx + 1) % fTxDescCnt;

		desc->des0 = (uint32)vector[i].base;
		desc->des1 = (uint32)(vector[i].base >> 32);
		desc->des2 = (uint32)vector[i].length;
		memory_full_barrier();
		desc->des3.val = DwmacDescDes3{
			.length = (uint32)totalLen,
			.ld = i == vectorCount - 1,
			.fd = i == 0,
			.own = true
		}.val;
	}
	fRegs->dma.channels[0].txEndAddr = ToPhysDmaAdr(GetTxDesc(fTxDescIdx));

	return descIdx;
}


status_t
DwmacDriver::Recv(generic_io_vec* vector, size_t vectorCount)
{
	if (vectorCount != 1)
		return B_ERROR;

	uint32 descIdx = fRxDescIdx;
	DwmacDesc* desc = GetTxDesc(fRxDescIdx);

	desc->des0 = 0;
	desc->des1 = 0;
	memory_full_barrier();
	desc->des0 = (uint32)vector[0].base;
	desc->des1 = (uint32)(vector[0].base >> 32);
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
