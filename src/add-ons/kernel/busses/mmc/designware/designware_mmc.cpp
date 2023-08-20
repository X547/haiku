#include <string.h>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/bus/MMC.h>
#include <dm2/device/Clock.h>
#include <dm2/device/Reset.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define DESIGNWARE_MMC_DRIVER_MODULE_NAME "busses/mmc/designware_mmc/driver/v1"


struct DesignwareMmcRegs {
	uint32 ctrl;
	uint32 pwren;
	uint32 clkdiv;
	uint32 clksrc;
	uint32 clkena;
	uint32 tmout;
	uint32 ctype;
	uint32 blksiz;
	uint32 bytcnt;
	uint32 intmask;
	uint32 cmdarg;
	uint32 cmd;
	uint32 resp0;
	uint32 resp1;
	uint32 resp2;
	uint32 resp3;
	uint32 mintsts;
	uint32 rintsts;
	uint32 status;
	uint32 fifoth;
	uint32 cdetect;
	uint32 wrtprt;
	uint32 gpio;
	uint32 tcmcnt;
	uint32 tbbcnt;
	uint32 debnce;
	uint32 usrid;
	uint32 verid;
	uint32 hcon;
	uint32 uhsReg;
	uint32 unknown1[2];
	uint32 bmod;
	uint32 pldmnd;
	uint32 dbaddr;
	uint32 idsts;
	uint32 idinten;
	uint32 dscaddr;
	uint32 bufaddr;
	uint32 unknown2[27];
	uint32 uhsRegExt;
	uint32 unknown3[61];
	uint32 data;
};

/* Interrupt Mask register */
#define DWMCI_INTMSK_ALL	0xffffffff
#define DWMCI_INTMSK_RE		(1 << 1)
#define DWMCI_INTMSK_CDONE	(1 << 2)
#define DWMCI_INTMSK_DTO	(1 << 3)
#define DWMCI_INTMSK_TXDR	(1 << 4)
#define DWMCI_INTMSK_RXDR	(1 << 5)
#define DWMCI_INTMSK_RCRC	(1 << 6)
#define DWMCI_INTMSK_DCRC	(1 << 7)
#define DWMCI_INTMSK_RTO	(1 << 8)
#define DWMCI_INTMSK_DRTO	(1 << 9)
#define DWMCI_INTMSK_HTO	(1 << 10)
#define DWMCI_INTMSK_FRUN	(1 << 11)
#define DWMCI_INTMSK_HLE	(1 << 12)
#define DWMCI_INTMSK_SBE	(1 << 13)
#define DWMCI_INTMSK_ACD	(1 << 14)
#define DWMCI_INTMSK_EBE	(1 << 15)

/* CTRL register */
#define DWMCI_CTRL_RESET		(1 << 0)
#define DWMCI_CTRL_FIFO_RESET	(1 << 1)
#define DWMCI_CTRL_DMA_RESET	(1 << 2)
#define DWMCI_DMA_EN			(1 << 5)
#define DWMCI_CTRL_SEND_AS_CCSD	(1 << 10)
#define DWMCI_IDMAC_EN			(1 << 25)
#define DWMCI_RESET_ALL			(DWMCI_CTRL_RESET | DWMCI_CTRL_FIFO_RESET | DWMCI_CTRL_DMA_RESET)

/* CMD register */
#define DWMCI_CMD_RESP_EXP		(1 << 6)
#define DWMCI_CMD_RESP_LENGTH	(1 << 7)
#define DWMCI_CMD_CHECK_CRC		(1 << 8)
#define DWMCI_CMD_DATA_EXP		(1 << 9)
#define DWMCI_CMD_RW			(1 << 10)
#define DWMCI_CMD_SEND_STOP		(1 << 12)
#define DWMCI_CMD_ABORT_STOP	(1 << 14)
#define DWMCI_CMD_SEND_INIT		(1 << 15)
#define DWMCI_CMD_PRV_DAT_WAIT	(1 << 13)
#define DWMCI_CMD_UPD_CLK		(1 << 21)
#define DWMCI_CMD_USE_HOLD_REG	(1 << 29)
#define DWMCI_CMD_START			(1 << 31)

/* CLKENA register */
#define DWMCI_CLKEN_ENABLE		(1 << 0)
#define DWMCI_CLKEN_LOW_PWR		(1 << 16)

/* Card-type registe */
#define DWMCI_CTYPE_1BIT		0
#define DWMCI_CTYPE_4BIT		(1 << 0)
#define DWMCI_CTYPE_8BIT		(1 << 16)

/* Status Register */
#define DWMCI_FIFO_EMPTY		(1 << 2)
#define DWMCI_FIFO_FULL			(1 << 3)
#define DWMCI_BUSY				(1 << 9)
#define DWMCI_FIFO_MASK			0x1fff
#define DWMCI_FIFO_SHIFT		17

/* FIFOTH Register */
#define MSIZE(x)				((x) << 28)
#define RX_WMARK(x)				((x) << 16)
#define TX_WMARK(x)				(x)
#define RX_WMARK_SHIFT			16
#define RX_WMARK_MASK			(0xfff << RX_WMARK_SHIFT)

/* UHS register */
#define DWMCI_DDR_MODE			(1 << 16)

/* Internal IDMAC interrupt defines */
#define DWMCI_IDINTEN_RI		(1 << 1)
#define DWMCI_IDINTEN_TI		(1 << 0)

#define DWMCI_IDINTEN_MASK		(DWMCI_IDINTEN_TI | DWMCI_IDINTEN_RI)


class DesignwareMmcDriver: public DeviceDriver {
public:
	DesignwareMmcDriver(DeviceNode* node): fNode(node), fMmcBus(*this) {}
	virtual ~DesignwareMmcDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	DesignwareMmcRegs volatile* fRegs {};
	uint64 fRegsLen {};

	ClockDevice* fCiuClock {};

	uint32 fFifoDepth {};
	uint32 fBusWidth = 4;
	uint32 fMaxFrequency {};
	uint64 fBusFreq {};
	uint32 fFifothVal {};

	uint64 fClockFreq {};
	bool fDdrMode {};
	bool fNeedInit = true;

	class MmcBusImpl: public BusDriver, public MmcBus {
	public:
		MmcBusImpl(DesignwareMmcDriver& base): fBase(base) {}

		// BusDriver
		void* QueryInterface(const char* name) final;

		// MmcBus
		status_t SetClock(uint32 kilohertz) final;
		status_t ExecuteCommand(uint8 command, uint32 argument, uint32* result) final;
		status_t DoIO(uint8 command, IOOperation* operation, bool offsetAsSectors) final;
		status_t SetBusWidth(int width) final;

	public:
		DesignwareMmcDriver& fBase;
	} fMmcBus;
};


template <typename T>
T div_round_up(T value, T div)
{
	return (value + (div - 1)) / div;
}


template <typename Proc>
status_t retry_count(Proc&& proc, uint32 count)
{
	for (; count > 0; count--) {
		if (proc())
			return B_OK;
	}
	dprintf("[!] timeout\n");
	return B_TIMED_OUT;
}


template <typename Proc>
status_t retry_timeout(Proc&& proc, bigtime_t absTimeout)
{
	while (system_time() < absTimeout) {
		if (proc())
			return B_OK;
	}
	dprintf("[!] timeout\n");
	return B_TIMED_OUT;
}


// #pragma mark - DesignwareMmcDriver

status_t
DesignwareMmcDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<DesignwareMmcDriver> driver(new(std::nothrow) DesignwareMmcDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


DesignwareMmcDriver::~DesignwareMmcDriver()
{
	ClockDevice* clock;
	for (uint32 i = 0; fFdtDevice->GetClock(i, &clock) >= B_OK; i++)
		clock->SetEnabled(false);

	ResetDevice* reset;
	for (uint32 i = 0; fFdtDevice->GetReset(i, &reset) >= B_OK; i++)
		reset->SetAsserted(true);
}


status_t
DesignwareMmcDriver::Init()
{
	dprintf("DesignwareMmcDriver::Init()\n");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	if (!fFdtDevice->GetReg(0, &regs, &fRegsLen))
		return B_ERROR;

	dprintf("  regs: %#" B_PRIx64 "\n", regs);

	fRegsArea.SetTo(map_physical_memory("Designware MMC MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	CHECK_RET(fFdtDevice->GetClockByName("ciu", &fCiuClock));

	ClockDevice* clock;
	for (uint32 i = 0; fFdtDevice->GetClock(i, &clock) >= B_OK; i++)
		clock->SetEnabled(true);

	ResetDevice* reset;
	for (uint32 i = 0; fFdtDevice->GetReset(i, &reset) >= B_OK; i++)
		reset->SetAsserted(false);

	//fBusFreq = fCiuClock->GetRate();
	fBusFreq = 49500000;
	dprintf("  fBusFreq: %" B_PRId64 "\n", fBusFreq);
	if (fBusFreq <= 0)
		return B_BAD_VALUE;

	CHECK_RET(fFdtDevice->GetPropUint32("fifo-depth", fFifoDepth));
	if (fFifoDepth < 8 || fFifoDepth > 4096)
		return B_BAD_VALUE;

	fFifothVal =
		MSIZE(0x2) |
		RX_WMARK(fFifoDepth / 2 - 1) |
		TX_WMARK(fFifoDepth / 2);

	fFdtDevice->GetPropUint32("bus-width", fBusWidth);
	fFdtDevice->GetPropUint32("max-frequency", fMaxFrequency);

	fRegs->pwren = 1;

	fRegs->ctrl = DWMCI_RESET_ALL;
	if (retry_count([this] {return (fRegs->ctrl & DWMCI_RESET_ALL) == 0;}, 1000) < B_OK) {
		dprintf("[!] reset failed\n");
		return B_IO_ERROR;
	}

	CHECK_RET(fMmcBus.SetClock(400));

	fRegs->rintsts = 0xFFFFFFFF;
	fRegs->intmask = 0;

	fRegs->tmout = 0xFFFFFFFF;

	fRegs->idinten = 0;
	fRegs->bmod = 0;

	fRegs->fifoth = fFifothVal;

	fRegs->clkena = 0;
	fRegs->clksrc = 0;

	fRegs->idinten = DWMCI_IDINTEN_MASK;

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "MMC Bus Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/mmc/driver/v1"}},
		{}
	};

	CHECK_RET(fNode->RegisterNode(this, static_cast<BusDriver*>(&fMmcBus), attrs, NULL));

	return B_OK;
}


// #pragma mark - MmcBusImpl

void*
DesignwareMmcDriver::MmcBusImpl::QueryInterface(const char* name)
{
	if (strcmp(name, MmcBus::ifaceName) == 0)
		return static_cast<MmcBus*>(this);

	return NULL;
}


status_t
DesignwareMmcDriver::MmcBusImpl::SetClock(uint32 kilohertz)
{
	dprintf("MmcBusImpl::SetClock(\"%" B_PRIu32 "\")\n", kilohertz);
	uint32 freq = kilohertz * 1000;
	if (freq == fBase.fClockFreq || freq == 0)
		return B_OK;

	uint64 sclk = fBase.fBusFreq;

	uint32 div = (sclk == freq) ? 0 : div_round_up<uint64>(sclk, 2 * freq);
	dprintf("  div: %" B_PRIu32 "\n", div);

	fBase.fRegs->clkena = 0;
	fBase.fRegs->clksrc = 0;

	fBase.fRegs->clkdiv = div;
	fBase.fRegs->cmd = DWMCI_CMD_PRV_DAT_WAIT | DWMCI_CMD_UPD_CLK | DWMCI_CMD_START;

	CHECK_RET(retry_count([this]() {
		return (fBase.fRegs->cmd & DWMCI_CMD_START) == 0;
	}, 10000));

	fBase.fRegs->clkena = DWMCI_CLKEN_ENABLE | DWMCI_CLKEN_LOW_PWR;
	fBase.fRegs->cmd = DWMCI_CMD_PRV_DAT_WAIT | DWMCI_CMD_UPD_CLK | DWMCI_CMD_START;

	CHECK_RET(retry_count([this]() {
		return (fBase.fRegs->cmd & DWMCI_CMD_START) == 0;
	}, 10000));

	fBase.fClockFreq = freq;
	return B_OK;
}


status_t
DesignwareMmcDriver::MmcBusImpl::ExecuteCommand(uint8 command, uint32 argument, uint32* result)
{
	bigtime_t startTime = system_time();

	CHECK_RET(retry_timeout([this] {return (fBase.fRegs->status & DWMCI_BUSY) == 0;}, startTime + 500));

	fBase.fRegs->rintsts = DWMCI_INTMSK_ALL;

	fBase.fRegs->cmdarg = argument;

	uint32 flags = 0;
	if (command == SD_STOP_TRANSMISSION)
		flags |= DWMCI_CMD_ABORT_STOP;
	else
		flags |= DWMCI_CMD_PRV_DAT_WAIT;

	if (result != NULL)
		flags |= DWMCI_CMD_RESP_EXP;

	if (fBase.fNeedInit) {
		fBase.fNeedInit = false;
		flags |= DWMCI_CMD_SEND_INIT;
	}

	flags |= (command | DWMCI_CMD_START | DWMCI_CMD_USE_HOLD_REG);

	fBase.fRegs->cmd = flags;

	uint32 mask;
	CHECK_RET(retry_count([this, &mask]{
		mask = fBase.fRegs->rintsts;
		return (mask & DWMCI_INTMSK_CDONE) != 0;
	}, 100000));
	fBase.fRegs->rintsts = mask;

	if (mask & DWMCI_INTMSK_RTO) {
		dprintf("[!] MmcBusImpl::ExecuteCommand: Response Timeout.\n");
		return B_TIMED_OUT;
	} else if (mask & DWMCI_INTMSK_RE) {
		dprintf("[!] MmcBusImpl::ExecuteCommand: Response Error.\n");
		return B_IO_ERROR;
	}

	if (result != NULL)
		*result = fBase.fRegs->resp0;

	snooze(100);

	return B_OK;
}


status_t
DesignwareMmcDriver::MmcBusImpl::DoIO(uint8 command, IOOperation* operation, bool offsetAsSectors)
{
	//TODO: implement
	return ENOSYS;
}


status_t
DesignwareMmcDriver::MmcBusImpl::SetBusWidth(int width)
{
	uint32 ctype;
	switch (width) {
	case 8:
		ctype = DWMCI_CTYPE_8BIT;
		break;
	case 4:
		ctype = DWMCI_CTYPE_4BIT;
		break;
	default:
		ctype = DWMCI_CTYPE_1BIT;
		break;
	}

	fBase.fRegs->ctype = ctype;

	uint32 uhsReg = fBase.fRegs->uhsReg;
	if (fBase.fDdrMode)
		uhsReg |= DWMCI_DDR_MODE;
	else
		uhsReg |= ~DWMCI_DDR_MODE;
	fBase.fRegs->uhsReg = uhsReg;

	return B_OK;
}


static driver_module_info sDesignwareMmcDriverModule = {
	.info = {
		.name = DESIGNWARE_MMC_DRIVER_MODULE_NAME,
	},
	.probe = DesignwareMmcDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sDesignwareMmcDriverModule,
	NULL
};
