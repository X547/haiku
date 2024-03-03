#include <string.h>
#include <stdio.h>
#include <new>
#include <algorithm>
#include <atomic>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/bus/MII.h>
#include <dm2/device/Clock.h>
#include <dm2/device/Reset.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <util/AutoLock.h>
#include <ContainerOf.h>
#include <ScopeExit.h>


#include <net/ether_driver.h>
#include <net/if_media.h>
#include <compat/dev/mii/mii.h>

#include <kernel.h>
#include <lock.h>
#include <condition_variable.h>
#include <arch/atomic.h>

#include "DwmacRegs.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define DWMAC_DRIVER_MODULE_NAME "drivers/network/dwmac/driver/v1"


template<typename Cond> static status_t
wait_for_cond(Cond cond, int32 attempts, bigtime_t retryInterval)
{
	for (; attempts > 0; attempts--) {
		if (cond())
			return B_OK;

		snooze(retryInterval);
	}
	return B_TIMED_OUT;
}


class DwmacDriver: public DeviceDriver {
public:
	virtual ~DwmacDriver();
	DwmacDriver(DeviceNode* node): fNode(node) {
		fCanReadCond.Init(this, "DwmacDriver::fCanReadCond");
		fCanWriteCond.Init(this, "DwmacDriver::fCanWriteCond");
	}

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	static const uint32 kDmaMinAlign = 32;
	static const uint32 kAxiBusWidth = 8;
	static const uint32 kDescSize = ROUNDUP(sizeof(DwmacDesc), kDmaMinAlign);
	static const uint32 kMaxPacketSize = ROUNDUP(1568, kDmaMinAlign);
	static const uint32 kDescCountTx = 32;
	static const uint32 kDescCountRx = 32;
	static const uint32 kDescCount = kDescCountTx + kDescCountRx;

private:
	status_t Init();

	status_t ConfigureMtl(uint32& tqs);
	status_t ConfigureMac();
	status_t ConfigureDma(uint32 tqs);

	void SetDuplex(bool isFullDuplex);
	void SetSpeed(uint32 speed);
	void SetClockRate(uint32 speed);

	status_t MdioWaitIdle();
	status_t MdioRead(uint32 addr, uint32 reg);
	status_t MdioWrite(uint32 addr, uint32 reg, uint16 value);

	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

	status_t GetSendPacket(uint8*& packet);
	status_t Send(uint8* packet, uint32 length);
	status_t Receive(uint8*& packet);
	status_t FreePacket(uint8* packet);

private:
	mutex	fLock = MUTEX_INITIALIZER("DwmacDriver");
	spinlock fSpinlock = B_SPINLOCK_INITIALIZER;

	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	DwmacRegs volatile* fRegs {};
	uint64 fRegsLen {};
	long fIrqVector = -1;
	bool fInterruptHandlerInstalled = false;

	ClockDevice* fTxClock {};
	ClockDevice* fRmiiRtxClock {};

	uint8 fMacAddr[6];

	AreaDeleter fDmaArea;
	size_t fDmaAreaSize {};
	uint8* fDmaAddr {};
	phys_addr_t fDmaPhysAddr {};

	uint8* fTxDescs {};
	uint8* fRxDescs {};
	phys_addr_t fTxDescsPhys {};
	phys_addr_t fRxDescsPhys {};
	uint32 fTxDescIdx {};
	uint32 fRxDescIdx {};

	uint8* fTxBuffer {};
	phys_addr_t fTxBufferPhys {};
	uint8* fRxBuffer {};
	phys_addr_t fRxBufferPhys {};

	std::atomic<int32> fOpenCount {};

	ConditionVariable fCanReadCond;
	ConditionVariable fCanWriteCond;

	ether_link_state fLinkState {.media = IFM_ETHER};
	sem_id fLinkStateChangeSem = -1;

	class MiiDevice: public BusDriver, ::MiiDevice {
	private:
		uint32 fAddress = 0;
		DeviceNode *fNode {};

	public:
		virtual ~MiiDevice() = default;
		DwmacDriver &Base() {return ContainerOf(*this, &DwmacDriver::fMiiDevice);}

		status_t InitDriver(DeviceNode* node) final;
		void* QueryInterface(const char* name) final;
		void DriverAttached(bool isAttached) final;

		status_t Read(uint32 reg) final;
		status_t Write(uint32 reg, uint16 value) final;
	} fMiiDevice;

	class DevFsNode: public ::DevFsNode, public ::DevFsNodeHandle {
	public:
		DwmacDriver& Base() {return ContainerOf(*this, &DwmacDriver::fDevFsNode);}

		Capabilities GetCapabilities() const final;
		status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

		status_t Close() final;
		status_t Read(off_t pos, void* buffer, size_t* length) final;
		status_t Write(off_t pos, const void* buffer, size_t* length) final;
		status_t Control(uint32 op, void *buffer, size_t length, bool isKernel) final;
	} fDevFsNode;
};


status_t
DwmacDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<DwmacDriver> driver(new(std::nothrow) DwmacDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


DwmacDriver::~DwmacDriver()
{
	ClockDevice* clock;
	for (uint32 i = 0; fFdtDevice->GetClock(i, &clock) >= B_OK; i++)
		clock->SetEnabled(false);

	ResetDevice* reset;
	for (uint32 i = 0; fFdtDevice->GetReset(i, &reset) >= B_OK; i++)
		reset->SetAsserted(true);

	if (fInterruptHandlerInstalled)
		remove_io_interrupt_handler(fIrqVector, HandleInterrupt, this);
}


status_t
DwmacDriver::Init()
{
	dprintf("DwmacDriver::Init()\n");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	if (!fFdtDevice->GetReg(0, &regs, &fRegsLen))
		return B_ERROR;

	dprintf("  regs: %#" B_PRIx64 "\n", regs);

	fRegsArea.SetTo(map_physical_memory("DWMAC MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	CHECK_RET(fFdtDevice->GetInterruptVectorByName("macirq", &fIrqVector));
	dprintf("  fIrqVector: %ld\n", fIrqVector);
	CHECK_RET(install_io_interrupt_handler(fIrqVector, HandleInterrupt, this, 0));
	fInterruptHandlerInstalled = true;

	CHECK_RET(fFdtDevice->GetClockByName("gtx", &fTxClock));
	CHECK_RET(fFdtDevice->GetClockByName("rmii_rtx", &fRmiiRtxClock));

	int propLen;
	const void* prop = fFdtDevice->GetProp("local-mac-address", &propLen);
	if (prop == NULL || propLen != sizeof(fMacAddr)) {
		return B_BAD_VALUE;
	}
	memcpy(fMacAddr, prop, sizeof(fMacAddr));
	dprintf("  MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n",
		fMacAddr[0], fMacAddr[1], fMacAddr[2],
		fMacAddr[3], fMacAddr[4], fMacAddr[5]
	);

	ClockDevice* clock;
	for (uint32 i = 0; fFdtDevice->GetClock(i, &clock) >= B_OK; i++)
		clock->SetEnabled(true);

	ResetDevice* reset;
	for (uint32 i = 0; fFdtDevice->GetReset(i, &reset) >= B_OK; i++)
		reset->SetAsserted(false);

	snooze(10);

	fRegs->dma.busMode.swr = true;

	if (wait_for_cond([this] {return fRegs->dma.busMode.swr == false;}, 50, 1000) < B_OK) {
		dprintf("[!] fRegs->dma.busMode.swr == true\n");
		return B_IO_ERROR;
	}

	fRegs->mac.usTicCounter = (125000000 /*fTxClock->GetRate()*/ / 1000000) - 1;

	dprintf("  gtx\n");
	dprintf("    enabled: %d\n", fTxClock->IsEnabled());
	dprintf("    rate: %" B_PRId64 " Hz\n", fTxClock->GetRate());
	dprintf("  rmii_rtx\n");
	dprintf("    enabled: %d\n", fRmiiRtxClock->IsEnabled());
	dprintf("    rate: %" B_PRId64 " Hz\n", fRmiiRtxClock->GetRate());

	if (!fTxClock->IsEnabled() || !fRmiiRtxClock->IsEnabled())
		return ENODEV;

	static const device_attr attrs[] = {
		{.name = B_DEVICE_PRETTY_NAME, .type = B_STRING_TYPE, .value = {.string = "MII Device"}},
		{.name = B_DEVICE_BUS,         .type = B_STRING_TYPE, .value = {.string = "mii"}},
		{}
	};
	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fMiiDevice), attrs, NULL));

	dprintf("  BMCR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_BMCR));
	dprintf("  BMSR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_BMSR));
	dprintf("  PHYIDR1: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_PHYIDR1));
	dprintf("  PHYIDR2: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_PHYIDR2));
	dprintf("  ANAR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_ANAR));
	dprintf("  ANLPAR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_ANLPAR));
	dprintf("  ANER: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_ANER));
	dprintf("  ANNP: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_ANNP));
	dprintf("  ANLPRNP: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_ANLPRNP));
	dprintf("  100T2CR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_100T2CR));
	dprintf("  100T2SR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_100T2SR));
	dprintf("  PSECR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_PSECR));
	dprintf("  PSESR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_PSESR));
	dprintf("  MMDACR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_MMDACR));
	dprintf("  MMDAADR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_MMDAADR));
	dprintf("  EXTSR: %#04" B_PRIx32 "\n", fMiiDevice.Read(MII_EXTSR));

	dprintf("  gtx rate: %" B_PRId64 " Hz\n", fTxClock->GetRate());
	dprintf("  rmii_rtx rate: %" B_PRId64 " Hz\n", fRmiiRtxClock->GetRate());

	auto ReserveDmaMem = [this](uint32 size) {
		uint32 offset = fDmaAreaSize;
		fDmaAreaSize += size;
		return offset;
	};

	uint32 txDescsOfs  = ReserveDmaMem(kDescCountTx * kDescSize);
	uint32 rxDescsOfs  = ReserveDmaMem(kDescCountRx * kDescSize);
	uint32 txBufferOfs = ReserveDmaMem(kDescCountTx * kMaxPacketSize);
	uint32 rxBufferOfs = ReserveDmaMem(kDescCountRx * kMaxPacketSize);

	fDmaArea.SetTo(create_area(
		"DWMAC DMA",
		(void**)&fDmaAddr, B_ANY_ADDRESS,
		fDmaAreaSize,
		B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
	));
	CHECK_RET(fDmaArea.Get());

	physical_entry pe;
	CHECK_RET(get_memory_map(fDmaAddr, B_PAGE_SIZE, &pe, 1));
	fDmaPhysAddr = pe.address;

	fTxDescs  = fDmaAddr + txDescsOfs;
	fRxDescs  = fDmaAddr + rxDescsOfs;
	fTxBuffer = fDmaAddr + txBufferOfs;
	fRxBuffer = fDmaAddr + rxBufferOfs;

	fTxDescsPhys  = fDmaPhysAddr + txDescsOfs;
	fRxDescsPhys  = fDmaPhysAddr + rxDescsOfs;
	fTxBufferPhys = fDmaPhysAddr + txBufferOfs;
	fRxBufferPhys = fDmaPhysAddr + rxBufferOfs;

	uint32 tqs;
	CHECK_RET(ConfigureMtl(tqs));
	CHECK_RET(ConfigureMac());
	CHECK_RET(ConfigureDma(tqs));


	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), "net/dwmac/%" B_PRId32, id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


status_t
DwmacDriver::ConfigureMtl(uint32& tqs)
{
	/* Enable Store and Forward mode for TX */
	/* Program Tx operating mode */
	fRegs->mtl.chan[0].txOpMode.tsf = true;
	fRegs->mtl.chan[0].txOpMode.txqen = DwmacMtlTxOpModeTxqen::enabled;
	/* Transmit Queue weight */
	fRegs->mtl.chan[0].txqWeight = 0x10;
	/* Enable Store and Forward mode for RX, since no jumbo frame */
	fRegs->mtl.chan[0].rxOpMode.rsf = true;
	/* Transmit/Receive queue fifo size; use all RAM for 1 queue */
	uint32 txFifoSz = fRegs->mac.hwFeature1.txFifoSize;
	uint32 rxFifoSz = fRegs->mac.hwFeature1.rxFifoSize;
	/*
	 * r/tx_fifo_sz is encoded as log2(n / 128). Undo that by shifting.
	 * r/tqs is encoded as (n / 256) - 1.
	 */
	tqs = (128 << txFifoSz) / 256 - 1;
	uint32 rqs = (128 << rxFifoSz) / 256 - 1;
	fRegs->mtl.chan[0].txOpMode.tqs = tqs;
	fRegs->mtl.chan[0].rxOpMode.rqs = rqs;
	/* Flow control used only if each channel gets 4KB or more FIFO */
	if (rqs >= ((4096 / 256) - 1)) {
		uint32 rfd, rfa;
		fRegs->mtl.chan[0].rxOpMode.val |= DwmacMtlRxOpMode {.ehfc = true}.val;
		/*
		 * Set Threshold for Activating Flow Contol space for min 2
		 * frames ie, (1500 * 1) = 1500 bytes.
		 *
		 * Set Threshold for Deactivating Flow Contol for space of
		 * min 1 frame (frame size 1500bytes) in receive fifo
		 */
		if (rqs == ((4096 / 256) - 1)) {
			/*
			 * This violates the above formula because of FIFO size
			 * limit therefore overflow may occur inspite of this.
			 */
			rfd = 0x3;	/* Full-3K */
			rfa = 0x1;	/* Full-1.5K */
		} else if (rqs == ((8192 / 256) - 1)) {
			rfd = 0x6;	/* Full-4K */
			rfa = 0xa;	/* Full-6K */
		} else if (rqs == ((16384 / 256) - 1)) {
			rfd = 0x6;	/* Full-4K */
			rfa = 0x12;	/* Full-10K */
		} else {
			rfd = 0x6;	/* Full-4K */
			rfa = 0x1E;	/* Full-16K */
		}
		fRegs->mtl.chan[0].rxOpMode.rfd = rfd;
		fRegs->mtl.chan[0].rxOpMode.rfa = rfa;
	}
	return B_OK;
}


status_t
DwmacDriver::ConfigureMac()
{
	fRegs->mac.rxqCtrl0.rxq0en = DwmacRxqCtrl0Rxq0en::enabledDcb;
	/* Multicast and Broadcast Queue Enable */
	fRegs->mac.rxqCtrl1 |= 0x00100000;
	/* enable promise mode */
	fRegs->mac.packetFilter |= 0x1;
	/* Set TX flow control parameters */
	/* Set Pause Time */
	fRegs->mac.qxTxFlowCtrl[0].pt = 0xffff;
	/* Assign priority for TX flow control */
	fRegs->mac.txqPrtyMap0.pstq0 = 0;
	/* Assign priority for RX flow control */
	fRegs->mac.rxqCtrl2.psrq0 = 0;
	/* Enable flow control */
	fRegs->mac.qxTxFlowCtrl[0].tfe = true;
	fRegs->mac.rxFlowCtrl.rfe = true;

	DwmacMacConfig config {.val = fRegs->mac.config.val};
	config.gpslce = false;
	config.wd = false;
	config.jd = false;
	config.je = false;
	config.cst = true;
	config.acs = true;
	fRegs->mac.config.val = config.val;

	fRegs->mac.addr[0].hi = (fMacAddr[5] << 8) | fMacAddr[4];
	fRegs->mac.addr[0].lo = (fMacAddr[3] << 24) | (fMacAddr[2] << 16) | (fMacAddr[1] << 8) | fMacAddr[0];

	return B_OK;
}


status_t
DwmacDriver::ConfigureDma(uint32 tqs)
{
	/* Enable OSP mode */
	fRegs->dma.channels[0].txControl.osp = true;
	/* RX buffer size. Must be a multiple of bus width */
	fRegs->dma.channels[0].rxControl.rbsz = kMaxPacketSize;

	uint32 descPad = (kDescSize - sizeof(DwmacDesc)) / kAxiBusWidth;
	fRegs->dma.channels[0].control.dsl = descPad;

	/*
	 * Burst length must be < 1/2 FIFO size.
	 * FIFO size in tqs is encoded as (n / 256) - 1.
	 * Each burst is n * 8 (PBLX8) * 16 (AXI width) == 128 bytes.
	 * Half of n * 256 is n * 128, so pbl == tqs, modulo the -1.
	 */
	uint32 pbl = tqs + 1;
	if (pbl > 32)
		pbl = 32;
	fRegs->dma.channels[0].txControl.txpbl = pbl;
	fRegs->dma.channels[0].rxControl.rxpbl = 8;

	/* DMA performance configuration */
	fRegs->dma.sysBusMode.val = DwmacDmaSysBusMode {
		.blen4 = true,
		.blen8 = true,
		.blen16 = true,
		.eame = true,
		.rdOsrLmt = 2,
	}.val;

	for (uint32 i = 0; i < kDescCountRx; i++) {
		DwmacDesc* desc = (DwmacDesc*)(fRxDescs + i * kDescSize);
		desc->des0 = (uint32)(fRxBufferPhys + i * kMaxPacketSize);
		desc->des3 = {.buf1v = true, .own = true};
		memory_full_barrier();
	}

	fRegs->dma.channels[0].txBaseAddrHi = (uint32)(fTxDescsPhys >> 32);
	fRegs->dma.channels[0].txBaseAddrLo = (uint32) fTxDescsPhys;
	fRegs->dma.channels[0].txRingLen = kDescCountTx - 1;

	fRegs->dma.channels[0].rxBaseAddrHi = (uint32)(fRxDescsPhys >> 32);
	fRegs->dma.channels[0].rxBaseAddrLo = (uint32) fRxDescsPhys;
	fRegs->dma.channels[0].rxRingLen = kDescCountRx - 1;

	/* Enable everything */
	fRegs->dma.channels[0].txControl.st = true;
	fRegs->dma.channels[0].rxControl.sr = true;
	fRegs->mac.config.re = true;
	fRegs->mac.config.te = true;

	/* TX tail pointer not written until we need to TX a packet */
	/*
	 * Point RX tail pointer at last descriptor. Ideally, we'd point at the
	 * first descriptor, implying all descriptors were available. However,
	 * that's not distinguishable from none of the descriptors being
	 * available.
	 */
	fRegs->dma.channels[0].rxEndAddr = fRxDescsPhys + (kDescCountRx - 1) * kDescSize;

	/* Enable interrupts */
	fRegs->mac.intEn = (1 << 0);
	fRegs->dma.channels[0].intrEna.val = 0xffffffff;
/*
	fRegs->dma.channels[0].intrEna.val = DwmacDmaChannelIntrEna {
		.tie = true,
		.rie = true
	}.val;
*/
	{
		DwmacDmaChannelStatus status {.val = fRegs->dma.channels[0].status.val};
		dprintf("dwmac: status: %#" B_PRIx32 "\n", status.val);
		DwmacDmaChannelIntrEna intrEna {.val = fRegs->dma.channels[0].intrEna.val};
		fRegs->dma.channels[0].status.val = status.val & intrEna.val;
	}

	return B_OK;
}


void
DwmacDriver::SetDuplex(bool isFullDuplex)
{
	fRegs->mac.config.dm = isFullDuplex;
	if (!isFullDuplex)
		fRegs->mtl.chan[0].txOpMode.ftq = true;
}


void
DwmacDriver::SetSpeed(uint32 speed)
{
	DwmacMacConfig config {.val = fRegs->mac.config.val};
	switch (speed) {
	case 10:
		config.ps = true;
		config.fes = false;
		break;
	case 100:
		config.ps = true;
		config.fes = true;
		break;
	case 1000:
		config.ps = false;
		config.fes = false;
		break;
	default:
		return;
	}
	fRegs->mac.config.val = config.val;
}


void
DwmacDriver::SetClockRate(uint32 speed)
{
	uint32 rate;
	switch (speed) {
	case 10:
		rate =   2500000;
		break;
	case 100:
		rate =  25000000;
		break;
	case 1000:
		rate = 125000000;
		break;
	default:
		return;
	}
	fTxClock->SetRate(rate);
	fRmiiRtxClock->SetRate(rate);
}


status_t
DwmacDriver::MdioWaitIdle()
{
	return wait_for_cond([&](){return !fRegs->mac.mdioAddr.gb;}, 1000000, 1);
}


status_t
DwmacDriver::MdioRead(uint32 addr, uint32 reg)
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

	return fRegs->mac.mdioData.gd;
}


status_t
DwmacDriver::MdioWrite(uint32 addr, uint32 reg, uint16 value)
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


int32
DwmacDriver::HandleInterrupt(void* arg)
{
	return static_cast<DwmacDriver*>(arg)->HandleInterruptInt();
}


int32
DwmacDriver::HandleInterruptInt()
{
	//dprintf("DwmacDriver::HandleInterrupt()\n");

	DwmacDmaChannelStatus status {.val = fRegs->dma.channels[0].status.val};
	DwmacDmaChannelIntrEna intrEna {.val = fRegs->dma.channels[0].intrEna.val};

	if (status.ri || status.eri)
		fCanReadCond.NotifyAll();

	if (status.ti || status.eti)
		fCanWriteCond.NotifyAll();
#if 0
	bool isFirst = true;
	auto Separator = [&isFirst]() {
		if (isFirst) {isFirst = false;} else {dprintf(",");}
	};
	dprintf("  status: {");
	if (status.ti ) {Separator(); dprintf("ti");}
	if (status.tps) {Separator(); dprintf("tps");}
	if (status.tbu) {Separator(); dprintf("tbu");}
	if (status.ri ) {Separator(); dprintf("ri");}
	if (status.rbu) {Separator(); dprintf("rbu");}
	if (status.rps) {Separator(); dprintf("rps");}
	if (status.rwt) {Separator(); dprintf("rwt");}
	if (status.eti) {Separator(); dprintf("eti");}
	if (status.eri) {Separator(); dprintf("eri");}
	if (status.fbe) {Separator(); dprintf("fbe");}
	if (status.cde) {Separator(); dprintf("cde");}
	if (status.ais) {Separator(); dprintf("ais");}
	if (status.nis) {Separator(); dprintf("nis");}
	if (status.teb) {Separator(); dprintf("teb");}
	if (status.reb) {Separator(); dprintf("reb");}
	dprintf("}\n");
#endif

	fRegs->dma.channels[0].status.val = status.val & intrEna.val;

	uint32 macIntStatus = fRegs->mac.intStatus;
	if (((1 << 0) & macIntStatus) != 0) {
		DwmacPhyifControlStatus phyifControlStatus {.val = fRegs->mac.phyifControlStatus.val};
		dprintf("dwmac: mac.phyifControlStatus %#" B_PRIx32 "\n", phyifControlStatus.val);
		if (phyifControlStatus.lnksts) {
			uint32 speed = 0;
			switch (phyifControlStatus.speed) {
				case DwmacPhyifControlStatusSpeed::speed2_5:
					speed = 10;
					break;
				case DwmacPhyifControlStatusSpeed::speed25:
					speed = 100;
					break;
				case DwmacPhyifControlStatusSpeed::speed125:
					speed = 1000;
					break;
			}
			bool duplex = phyifControlStatus.lnkmod;

			SetDuplex(duplex);
			SetSpeed(speed);
			SetClockRate(speed);

			{
				SpinLocker lock(fSpinlock);
				fLinkState = {
					.media = IFM_ETHER | IFM_ACTIVE,
					.quality = 1000,
					.speed = speed * 1000000ULL
				};
				// TODO: more precise detection (T vs TX etc.)
				switch (speed) {
					case 10:
						fLinkState.media |= IFM_10_T;
						break;
					case 100:
						fLinkState.media |= IFM_100_TX;
						break;
					case 1000:
						fLinkState.media |= IFM_1000_T;
						break;
				}
				if (duplex)
					fLinkState.media |= IFM_FULL_DUPLEX;
				else
					fLinkState.media |= IFM_HALF_DUPLEX;
			}

			const char* duplexStr = duplex ? "full" : "half";
			dprintf("dwmac: link up: %" B_PRIu32" %s\n", speed, duplexStr);
		} else {
			dprintf("dwmac: link down\n");
			SpinLocker lock(fSpinlock);
			fLinkState = {
				.media = IFM_ETHER,
			};
		}
		SpinLocker lock(fSpinlock);
		release_sem_etc(fLinkStateChangeSem, 1, B_DO_NOT_RESCHEDULE);
	}

	return B_HANDLED_INTERRUPT;
}


status_t
DwmacDriver::GetSendPacket(uint8*& packet)
{
	DwmacDesc* desc = (DwmacDesc*)(fTxDescs + fTxDescIdx * kDescSize);
	if (desc->des3.own)
		return EAGAIN;

	packet = fTxBuffer + fTxDescIdx * kMaxPacketSize;
	return B_OK;
}


status_t
DwmacDriver::Send(uint8* packet, uint32 length)
{
	uint8* expectedPacket = fTxBuffer + fTxDescIdx * kMaxPacketSize;
#if 0
	dprintf("DwmacDriver::Send(%p, %" B_PRIu32 ")\n", packet, length);
	dprintf("  expectedPacket: %p\n", expectedPacket);
	dprintf("  fTxDescIdx: %" B_PRIu32 "\n", fTxDescIdx);
#endif
	if (packet != expectedPacket)
		return EINVAL;

	DwmacDesc* desc = (DwmacDesc*)(fTxDescs + fTxDescIdx * kDescSize);

	phys_addr_t physAddr = fTxBufferPhys + fTxDescIdx * kMaxPacketSize;
	desc->des0 = (uint32) physAddr;
	desc->des1 = (uint32)(physAddr >> 32);
	desc->des2 = length;
	/*
	 * Make sure that if HW sees the _OWN write below, it will see all the
	 * writes to the rest of the descriptor too.
	 */
	memory_full_barrier();
	desc->des3 = {
		.length = length,
		.ld = true,
		.fd = true,
		.own = true
	};

	//dprintf("DwmacDriver::EnqueueTx\n");
	fTxDescIdx = (fTxDescIdx + 1) % kDescCountTx;
	fRegs->dma.channels[0].txEndAddr = fTxDescsPhys + fTxDescIdx * kDescSize;

	return B_OK;
}


status_t
DwmacDriver::Receive(uint8*& packet)
{
	DwmacDesc* desc = (DwmacDesc*)(fRxDescs + fRxDescIdx * kDescSize);
	if (desc->des3.own)
		return EAGAIN;

	packet = fRxBuffer + fRxDescIdx * kMaxPacketSize;
	return desc->des3.length;
}


status_t
DwmacDriver::FreePacket(uint8* packet)
{
	uint8* expectedPacket = fRxBuffer + fRxDescIdx * kMaxPacketSize;
#if 0
	dprintf("DwmacDriver::FreePacket(%p)\n", packet);
	dprintf("  expectedPacket: %p\n", expectedPacket);
	dprintf("  fRxDescIdx: %" B_PRIu32 "\n", fRxDescIdx);
#endif
	if (packet != expectedPacket)
		return EINVAL;

	DwmacDesc* desc = (DwmacDesc*)(fRxDescs + fRxDescIdx * kDescSize);
	desc->des0 = 0;
	memory_full_barrier();
	phys_addr_t physAddr = fRxBufferPhys + fRxDescIdx * kMaxPacketSize;
	desc->des0 = (uint32) physAddr;
	desc->des1 = (uint32)(physAddr >> 32);
	desc->des2 = 0;
	/*
	 * Make sure that if HW sees the _OWN write below, it will see all the
	 * writes to the rest of the descriptor too.
	 */
	memory_full_barrier();
	desc->des3 = {.buf1v = true, .own = true};

	//dprintf("DwmacDriver::EnqueueRx\n");
	fRegs->dma.channels[0].rxEndAddr = fRxDescsPhys + fRxDescIdx * kDescSize;

	fRxDescIdx = (fRxDescIdx + 1) % kDescCountRx;

	return B_OK;
}


// #pragma mark - DwmacDriver::MiiDevice

status_t
DwmacDriver::MiiDevice::InitDriver(DeviceNode* node)
{
	fNode = node;
	return B_OK;
}


void*
DwmacDriver::MiiDevice::QueryInterface(const char* name)
{
	if (strcmp(name, ::MiiDevice::ifaceName) == 0)
		return static_cast<::MiiDevice*>(this);

	return NULL;
}


void
DwmacDriver::MiiDevice::DriverAttached(bool isAttached)
{
}


status_t
DwmacDriver::MiiDevice::Read(uint32 reg)
{
	return Base().MdioRead(fAddress, reg);
}


status_t
DwmacDriver::MiiDevice::Write(uint32 reg, uint16 value)
{
	return Base().MdioWrite(fAddress, reg, value);
}


// #pragma mark - DwmacDriver::DevFsNode

DevFsNode::Capabilities
DwmacDriver::DevFsNode::GetCapabilities() const
{
	return {.read = true, .write = true, .control = true};
}


status_t
DwmacDriver::DevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	if (Base().fOpenCount++ != 0) {
		Base().fOpenCount--;
		return B_BUSY;
	}

	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
DwmacDriver::DevFsNode::Close()
{
	if (Base().fOpenCount-- != 1)
		return B_OK;

	return B_OK;
}


status_t
DwmacDriver::DevFsNode::Read(off_t pos, void* buffer, size_t* numBytes)
{
	uint8* packet {};
	int32 length {};
	for (;;) {
		ConditionVariableEntry cvEntry;
		Base().fCanReadCond.Add(&cvEntry);
		length = Base().Receive(packet);
		if (length == EAGAIN && Base().fOpenCount > 0) {
			cvEntry.Wait();
			continue;
		}
#if 0
		dprintf("DwmacDriver::Receive(%p): %" B_PRId32 "\n", packet, length);
#endif
		if (length < 0) {
			*numBytes = 0;
			return length;
		}
		break;
	}
	Base().FreePacket(packet);
	*numBytes = std::min<size_t>(*numBytes, length);
	user_memcpy(buffer, packet, *numBytes);
	return B_OK;
}


status_t
DwmacDriver::DevFsNode::Write(off_t pos, const void* buffer, size_t* numBytes)
{
	uint8* packet {};
	for (;;) {
		ConditionVariableEntry cvEntry;
		Base().fCanWriteCond.Add(&cvEntry);
		status_t res = Base().GetSendPacket(packet);
		if (res == EAGAIN && Base().fOpenCount > 0) {
			cvEntry.Wait();
			continue;
		}
#if 0
		dprintf("DwmacDriver::Send(%p): %" B_PRIuSIZE "\n", packet, *numBytes);
#endif
		if (res < 0) {
			*numBytes = 0;
			return res;
		}
		break;
	}
	user_memcpy(packet, buffer, *numBytes);
	status_t res = Base().Send(packet, *numBytes);
	if (res < B_OK) {
		*numBytes = 0;
		return res;
	}
	return B_OK;
}


status_t
DwmacDriver::DevFsNode::Control(uint32 op, void *buffer, size_t length, bool isKernel)
{
	switch (op) {
		case ETHER_INIT:
			return B_OK;

		case ETHER_GETADDR:
			CHECK_RET(user_memcpy(buffer, &Base().fMacAddr, sizeof(Base().fMacAddr)));
			return B_OK;

		case ETHER_GETFRAMESIZE: {
			uint32 value = 1568;
			CHECK_RET(user_memcpy(buffer, &value, sizeof(value)));
			return B_OK;
		}
		case ETHER_SET_LINK_STATE_SEM: {
			sem_id value;
			CHECK_RET(user_memcpy(&value, buffer, sizeof(value)));
			InterruptsSpinLocker lock(Base().fSpinlock);
			Base().fLinkStateChangeSem = value;
			return B_OK;
		}
		case ETHER_GET_LINK_STATE: {
			InterruptsSpinLocker lock(Base().fSpinlock);
			CHECK_RET(user_memcpy(buffer, &Base().fLinkState, sizeof(Base().fLinkState)));
			return B_OK;
		}
	}

	return B_DEV_INVALID_IOCTL;
}


static driver_module_info sDwmacDriverModule = {
	.info = {
		.name = DWMAC_DRIVER_MODULE_NAME,
	},
	.probe = DwmacDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sDwmacDriverModule,
	NULL
};
