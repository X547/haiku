#include <string.h>
#include <stdio.h>
#include <new>
#include <algorithm>
#include <atomic>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/device/Clock.h>
#include <dm2/device/Reset.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <ContainerOf.h>

#include <net/ether_driver.h>
#include <net/if_media.h>

#include <kernel.h>
#include <locks.h>
#include <arch/atomic.h>

#include "DwmacRegs.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define DWMAC_DRIVER_MODULE_NAME "drivers/network/dwmac/driver/v1"


template <typename Proc>
status_t retry_count(Proc&& proc, uint32 count)
{
	for (; count > 0; count--) {
		if (proc())
			return B_OK;
		snooze(1000);
	}
	dprintf("[!] timeout\n");
	return B_TIMED_OUT;
}


class DwmacDriver: public DeviceDriver {
public:
	DwmacDriver(DeviceNode* node): fNode(node) {}
	virtual ~DwmacDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	static const uint32 kDmaMinAlign = 32;
	static const uint32 kAxiBusWidth = 8;
	static const uint32 kDescSize = ROUNDUP(sizeof(DwmacDesc), kDmaMinAlign);
	static const uint32 kMaxPacketSize = ROUNDUP(1568, kDmaMinAlign);
	static const uint32 kDescCountTx = 4;
	static const uint32 kDescCountRx = 4;
	static const uint32 kDescCount = kDescCountTx + kDescCountRx;

private:
	status_t Init();

	status_t ConfigureMtl(uint32& tqs);
	status_t ConfigureMac();
	status_t ConfigureDma(uint32 tqs);

	status_t Receive(uint8*& packet);
	status_t FreePacket(uint8* packet);

private:
	mutex	fLock = MUTEX_INITIALIZER("DwmacDriver");

	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	DwmacRegs volatile* fRegs {};
	uint64 fRegsLen {};

	ClockDevice* fTxClock {};
	ClockDevice* fRmiiRtxClock {};

	uint8 fMacAddr[6];

	AreaDeleter fDmaArea;
	size_t fDmaAreaSize {};
	uint8* fDmaAddr {};
	phys_addr_t fDmaPhysAddr {};

	DwmacDesc* fTxDescs {};
	DwmacDesc* fRxDescs {};
	phys_addr_t fTxDescsPhys {};
	phys_addr_t fRxDescsPhys {};
	uint32 fRxDescIdx {};

	uint8* fRxBuffer {};
	phys_addr_t fRxBufferPhys {};

	std::atomic<int32> fOpenCount {};

	sem_id fLinkStateChangeSem = -1;


	class DevFsNode: public ::DevFsNode, public ::DevFsNodeHandle {
	public:
		DwmacDriver &Base() {return ContainerOf(*this, &DwmacDriver::fDevFsNode);}

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

	if (retry_count([this] {return fRegs->dma.busMode.swr == false;}, 50) < B_OK) {
		dprintf("[!] fRegs->dma.busMode.swr == true\n");
		return B_IO_ERROR;
	}

	//fRegs->mac.usTicCounter = (fTxClock->GetRate() / 1000000) - 1;

	dprintf("  gtx\n");
	dprintf("    enabled: %d\n", fTxClock->IsEnabled());
	dprintf("    rate: %" B_PRId64 " Hz\n", fTxClock->GetRate());
	dprintf("  rmii_rtx\n");
	dprintf("    enabled: %d\n", fRmiiRtxClock->IsEnabled());
	dprintf("    rate: %" B_PRId64 " Hz\n", fRmiiRtxClock->GetRate());

	if (!fTxClock->IsEnabled() || !fRmiiRtxClock->IsEnabled())
		return ENODEV;

	uint32 txDescsOfs = fDmaAreaSize;
	fDmaAreaSize += fDmaAreaSize + kDescCountTx * kDescSize;
	uint32 rxDescsOfs = fDmaAreaSize;
	fDmaAreaSize += fDmaAreaSize + kDescCountRx * kDescSize;
	uint32 rxBufferOfs = fDmaAreaSize;
	fDmaAreaSize += kDescCountRx * kMaxPacketSize;

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

	fTxDescs = (DwmacDesc*)(fDmaAddr + txDescsOfs);
	fRxDescs = (DwmacDesc*)(fDmaAddr + rxDescsOfs);
	fRxBuffer =             fDmaAddr + rxBufferOfs;

	fTxDescsPhys  = fDmaPhysAddr + txDescsOfs;
	fRxDescsPhys  = fDmaPhysAddr + rxDescsOfs;
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

	fRegs->mac.config.gpslce = false;
	fRegs->mac.config.wd = false;
	fRegs->mac.config.jd = false;
	fRegs->mac.config.je = false;
	fRegs->mac.config.cst = true;
	fRegs->mac.config.acs = true;

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
		DwmacDesc* desc = &fRxDescs[i];
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

	return B_OK;
}


status_t
DwmacDriver::Receive(uint8*& packet)
{
	DwmacDesc* desc = &fRxDescs[fRxDescIdx];
	if (desc->des3.own)
		return EAGAIN;

	packet = fRxBuffer + fRxDescIdx * kMaxPacketSize;
	return desc->des3.length;
}


status_t
DwmacDriver::FreePacket(uint8* packet)
{
	uint8* expectedPacket = fRxBuffer + fRxDescIdx * kMaxPacketSize;
	if (packet != expectedPacket)
		return EINVAL;

	DwmacDesc* desc = &fRxDescs[fRxDescIdx];
	desc->des0 = 0;
	memory_full_barrier();
	desc->des0 = fRxBufferPhys + fRxDescIdx * kMaxPacketSize;
	desc->des1 = 0;
	desc->des2 = 0;
	/*
	 * Make sure that if HW sees the _OWN write below, it will see all the
	 * writes to the rest of the descriptor too.
	 */
	memory_full_barrier();
	desc->des3 = {.buf1v = true, .own = true};

	fRegs->dma.channels[0].rxEndAddr = fRxDescsPhys + fRxDescIdx * kDescSize;

	fRxDescIdx = (fRxDescIdx + 1) % kDescCountRx;

	return 0;
}


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
	uint8* packet;
	int32 length;
	for (;;) {
		length = Base().Receive(packet);
		if (length == EAGAIN && Base().fOpenCount > 0) {
			snooze(10000);
			continue;
		}
		if (length < 0) {
			*numBytes = 0;
			return length;
		}
	}
	Base().FreePacket(packet);
	*numBytes = std::min<size_t>(*numBytes, length);
	user_memcpy(buffer, packet, *numBytes);
	return B_OK;
}


status_t
DwmacDriver::DevFsNode::Write(off_t pos, const void* buffer, size_t* numBytes)
{
	*numBytes = 0;
	return B_IO_ERROR;
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
			Base().fLinkStateChangeSem = value;
			return B_OK;
		}
		case ETHER_GET_LINK_STATE: {
			ether_link_state state {
				.media = IFM_ETHER | IFM_FULL_DUPLEX | IFM_ACTIVE,
				.quality = 1000,
				.speed = 1000 * 1000 * 1000, // 1Gbps
			};
			CHECK_RET(user_memcpy(buffer, &state, sizeof(state)));
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
