#include <assert.h>
#include <string.h>
#include <sys/uio.h>
#include <algorithm>
#include <new>

#include <KernelExport.h>

#include <dm2/bus/FDT.h>
#include <dm2/bus/MMC.h>
#include <dm2/device/Clock.h>
#include <dm2/device/Reset.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <ScopeExit.h>

#include <kernel.h>
#include <condition_variable.h>
#include <util/iovec_support.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define CHECK_RET_MSG(err, msg...) \
	{ \
		status_t _err = (err); \
		if (_err < B_OK) { \
			dprintf(msg); \
			return _err; \
		} \
	} \

#define VOLATILE_ASSIGN_QUIRKS(Type) \
	void operator=(const Type& rhs) volatile \
	{ \
		value = rhs.value; \
	} \
	 \
	void operator=(const volatile Type& rhs) volatile \
	{ \
		value = rhs.value; \
	} \


#define DESIGNWARE_MMC_DRIVER_MODULE_NAME "busses/mmc/designware_mmc/driver/v1"


union DesignwareMmcCmd {
	struct {
		uint32 indx:       6; //  0
		uint32 respExp:    1; //  6
		uint32 respLong:   1; //  7
		uint32 respCrc:    1; //  8
		uint32 datExp:     1; //  9
		uint32 datWr:      1; // 10
		uint32 strmMode:   1; // 11
		uint32 sendStop:   1; // 12
		uint32 prvDatWait: 1; // 13
		uint32 stop:       1; // 14
		uint32 init:       1; // 15
		uint32 unknown1:   5; // 16
		uint32 updClk:     1; // 21
		uint32 ceataRd:    1; // 22
		uint32 ccsExp:     1; // 23
		uint32 unknown2:   4; // 24
		uint32 voltSwitch: 1; // 28
		uint32 useHoldReg: 1; // 29
		uint32 unknown3:   1; // 30
		uint32 start:      1; // 31
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcCmd)
};

union DesignwareMmcCtrl {
	struct {
		uint32 reset:        1; //  0
		uint32 fifoReset:    1; //  1
		uint32 dmaReset:     1; //  2
		uint32 unknown2:     1; //  3
		uint32 intEnable:    1; //  4
		uint32 dmaEnable:    1; //  5
		uint32 readWait:     1; //  6
		uint32 sendIrqResp:  1; //  7
		uint32 abrtReadData: 1; //  8
		uint32 sendCcsd:     1; //  9
		uint32 sendAsCcsd:   1; // 10
		uint32 ceataIntEn:   1; // 11
		uint32 unknown3:    13; // 12
		uint32 useIdmac:     1; // 25
		uint32 unknown4:     6; // 26
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcCtrl)
};

static const DesignwareMmcCtrl kDesignwareMmcCtrlResetAll = {
	.reset = true,
	.fifoReset = true,
	.dmaReset = true,
};

enum class DesignwareMmcCardType: uint32 {
	bit1 = 0,
	bit4 = 1 << 0,
	bit8 = 1 << 16
};

union DesignwareMmcStatus {
	struct {
		uint32 unknown1:   2; //  0
		uint32 fifoEmpty:  1; //  2
		uint32 fifoFull:   1; //  3
		uint32 unknown2:   5; //  4
		uint32 busy:       1; //  9
		uint32 unknown3:   7; // 10
		uint32 fcnt:      13; // 17
		uint32 unknown4:   1; // 30
		uint32 dmaReq:     1; // 31
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcStatus)
};

union DesignwareMmcInt {
	struct {
		uint32 cd:        1; //  0
		uint32 respError: 1; //  1
		uint32 cmdDone:   1; //  2
		uint32 dataOver:  1; //  3
		uint32 txdr:      1; //  4
		uint32 rxdr:      1; //  5
		uint32 rcrc:      1; //  6
		uint32 dcrc:      1; //  7
		uint32 rto:       1; //  8
		uint32 drto:      1; //  9
		uint32 hto:       1; // 10
		uint32 frun:      1; // 11
		uint32 hle:       1; // 12
		uint32 sbe:       1; // 13
		uint32 acd:       1; // 14
		uint32 ebe:       1; // 15
		uint32 unknown1: 16; // 16
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcInt)
};

static const DesignwareMmcInt kDesignwareMmcIntAll = {
	.value = 0xffffffff
};

static const DesignwareMmcInt kDesignwareMmcIntDataError = {
	.dcrc = true,
	.frun = true,
	.hle = true,
	.sbe = true,
	.ebe = true,
};

static const DesignwareMmcInt kDesignwareMmcIntDataTimeout = {
	.drto = true,
	.hto = true,
};

static const DesignwareMmcInt kDesignwareMmcIntCmdError = {
	.respError = true,
	.rcrc = true,
	.rto = true,
	.hle = true,
};

union DesignwareMmcFifoth {
	struct {
		uint32 txWmark: 12; //  0
		uint32 unknown1: 4; // 12
		uint32 rxWmark: 12; // 16
		uint32 mSize:    3; // 28
		uint32 unknown2: 1; // 31
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcFifoth)
};

union DesignwareMmcDmacBmod {
	struct {
		uint32 swReset:   1; //  0
		uint32 fb:        1; //  1
		uint32 unknown1:  5; //  2
		uint32 enable:    1; //  7
		uint32 unknown2: 24; //  8
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcDmacBmod)
};

enum struct DesignwareMmcDmacHconTransMode: uint32 {
	idma  = 0,
	dwdma = 1,
	gdma  = 2,
	nodma = 3,
};

union DesignwareMmcDmacHcon {
	struct {
		uint32 unknown1:   1; //  0
		uint32 slotNum:    5; //  1
		uint32 unknown2:   1; //  6
		uint32 hdataWidth: 3; //  7
		uint32 unknown3:   6; // 10
		DesignwareMmcDmacHconTransMode
		       transMode:  2; // 16
		uint32 unknown4:   9; // 18
		uint32 addrConfig: 1; // 27
		uint32 unknown5:   4; // 28
	};
	uint32 value;

	VOLATILE_ASSIGN_QUIRKS(DesignwareMmcDmacHcon)
};

struct DesignwareMmcRegs {
	DesignwareMmcCtrl ctrl;
	uint32 pwren;
	uint32 clkdiv;
	uint32 clksrc;
	uint32 clkena;
	uint32 tmout;
	DesignwareMmcCardType ctype;
	uint32 blksiz;
	uint32 bytcnt;
	DesignwareMmcInt intmask;
	uint32 cmdarg;
	DesignwareMmcCmd cmd;
	uint32 resp0;
	uint32 resp1;
	uint32 resp2;
	uint32 resp3;
	DesignwareMmcInt mintsts;
	DesignwareMmcInt rintsts;
	DesignwareMmcStatus status;
	DesignwareMmcFifoth fifoth;
	uint32 cdetect;
	uint32 wrtprt;
	uint32 gpio;
	uint32 tcmcnt;
	uint32 tbbcnt;
	uint32 debnce;
	uint32 usrid;
	uint32 verid;
	DesignwareMmcDmacHcon hcon;
	uint32 uhsReg;
	uint32 unknown1[2];
	DesignwareMmcDmacBmod bmod;
	uint32 pldmnd;
	union {
		struct {
			uint32 dbaddr;
			uint32 idsts;
			uint32 idinten;
			uint32 dscaddr;
			uint32 bufaddr;
			uint32 unknown2_1[27];
		};
		struct {
			uint32 dbaddrl;
			uint32 dbaddru;
			uint32 idsts64;
			uint32 idinten64;
			uint32 dscaddrl;
			uint32 dscaddru;
			uint32 bufaddrl;
			uint32 bufaddru;
			uint32 unknown2_2[24];
		};
	};
	uint32 uhsRegExt;
	uint32 unknown3[61];
	uint32 data;
};

union DesignwareMmcIdmacDescFlags {
	struct {
		uint32 unknown1: 2;  //  0
		uint32 ld: 1;        //  2
		uint32 fs: 1;        //  3
		uint32 ch: 1;        //  4
		uint32 unknown2: 26; //  5
		uint32 own: 1;       // 31
	};
	uint32 value;
};

struct DesignwareMmcIdmacDesc {
	DesignwareMmcIdmacDescFlags flags;
	uint32 cnt;
	uint32 addr;
	uint32 nextAddr;
};


/* CLKENA register */
#define DWMCI_CLKEN_ENABLE		(1 << 0)
#define DWMCI_CLKEN_LOW_PWR		(1 << 16)

/* UHS register */
#define DWMCI_DDR_MODE			(1 << 16)

/* Internal IDMAC interrupt defines */
#define DWMCI_IDINTEN_NI		(1 << 8)
#define DWMCI_IDINTEN_RI		(1 << 1)
#define DWMCI_IDINTEN_TI		(1 << 0)

#define DWMCI_IDINTEN_MASK		(DWMCI_IDINTEN_TI | DWMCI_IDINTEN_RI | DWMCI_IDINTEN_NI)


class DesignwareMmcDriver: public DeviceDriver {
public:
	DesignwareMmcDriver(DeviceNode* node): fNode(node), fMmcBus(*this) {}
	virtual ~DesignwareMmcDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();
	status_t ExecuteCommand(const mmc_command& cmd, const mmc_data* data);
	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fRegsArea;
	DesignwareMmcRegs volatile* fRegs {};
	uint64 fRegsLen {};

	long fIrqVector = -1;
	bool fInterruptHandlerInstalled = false;

	ClockDevice* fCiuClock {};

	uint32 fFifoDepth {};
	uint32 fBusWidth = 4;
	uint32 fMaxFrequency {};
	uint64 fBusFreq {};
	DesignwareMmcFifoth fFifothVal {};

	uint64 fClockFreq {};
	bool fDdrMode {};
	bool fNeedInit = true;

	AreaDeleter fDmaDescsArea;
	uint32 fDmaDescCnt = 256;
	DesignwareMmcIdmacDesc* fDmaDescs {};
	phys_addr_t fDmaDescsPhysAdr {};

	ConditionVariable fCmdCompletedCond;
	ConditionVariable fDataOverCond;

	class MmcBusImpl: public BusDriver, public MmcBus {
	public:
		MmcBusImpl(DesignwareMmcDriver& base): fBase(base) {}

		// BusDriver
		void* QueryInterface(const char* name) final;

		// MmcBus
		status_t SetClock(uint32 kilohertz) final;
		status_t ExecuteCommand(uint8 command, uint32 argument, uint32* result) final;
		status_t SetBusWidth(int width) final;

		status_t ExecuteCommand(const mmc_command& cmd, const mmc_data* data) final;

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


static void
dump_status(DesignwareMmcStatus status)
{
	bool isFirst = true;
	auto WriteSep = [&isFirst] {if (isFirst) {isFirst = false;} else {dprintf(", ");}};
	dprintf("(");
	if (status.fifoEmpty) {
		WriteSep();
		dprintf("fifoEmpty");
	}
	if (status.fifoFull) {
		WriteSep();
		dprintf("fifoFull");
	}
	if (status.busy) {
		WriteSep();
		dprintf("busy");
	}
	if (status.fcnt > 0) {
		WriteSep();
		dprintf("fcnt: %" B_PRIu32, status.fcnt);
	}
	if (status.dmaReq) {
		WriteSep();
		dprintf("dmaReq");
	}
	dprintf(")");
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

	if (fInterruptHandlerInstalled)
		remove_io_interrupt_handler(fIrqVector, HandleInterrupt, this);
}


status_t
DesignwareMmcDriver::Init()
{
	dprintf("DesignwareMmcDriver::Init()\n");

	fCmdCompletedCond.Init(this, "fCmdCompletedCond");
	fDataOverCond.Init(this, "fDataOverCond");

	fFdtDevice = fNode->QueryBusInterface<FdtDevice>();

	uint64 regs = 0;
	if (!fFdtDevice->GetReg(0, &regs, &fRegsLen))
		return B_ERROR;

	dprintf("  regs: %#" B_PRIx64 "\n", regs);

	switch (regs) {
		case 0x16010000:
			fIrqVector = 74;
			break;
		case 0x16020000:
			fIrqVector = 75;
			break;
	}

	dprintf("  irqVector: %ld\n", fIrqVector);

	fRegsArea.SetTo(map_physical_memory("Designware MMC MMIO", regs, fRegsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	fDmaDescsArea.SetTo(create_area(
		"idmac",
		(void**)&fDmaDescs, B_ANY_ADDRESS,
		ROUNDUP(fDmaDescCnt * sizeof(DesignwareMmcIdmacDesc), B_PAGE_SIZE),
		B_32_BIT_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
	));
	CHECK_RET(fDmaDescsArea.Get());

	physical_entry pe;
	CHECK_RET(get_memory_map(fDmaDescs, B_PAGE_SIZE, &pe, 1));
	fDmaDescsPhysAdr = pe.address;

	if (fIrqVector >= 0) {
		CHECK_RET(install_io_interrupt_handler(fIrqVector, HandleInterrupt, this, 0));
		fInterruptHandlerInstalled = true;
	}

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

	fFifothVal = {
		.txWmark = fFifoDepth / 2,
		.rxWmark = fFifoDepth / 2 - 1,
		.mSize = 2,
	};
	dprintf("  fFifothVal: %#" B_PRIx32 "\n", fFifothVal.value);

	fFdtDevice->GetPropUint32("bus-width", fBusWidth);
	fFdtDevice->GetPropUint32("max-frequency", fMaxFrequency);

	fRegs->pwren = 1;

	fRegs->ctrl = kDesignwareMmcCtrlResetAll;
	if (retry_count([this] {return (fRegs->ctrl.value & kDesignwareMmcCtrlResetAll.value) == 0;}, 1000) < B_OK) {
		dprintf("[!] reset failed\n");
		return B_IO_ERROR;
	}

	CHECK_RET(fMmcBus.SetClock(400));

	fRegs->rintsts = kDesignwareMmcIntAll;
	fRegs->intmask = {};

	fRegs->tmout = 0xFFFFFFFF;

	fRegs->idinten = 0;
	fRegs->bmod = {.swReset = 1};

	fRegs->fifoth = fFifothVal;

	fRegs->clkena = 0;
	fRegs->clksrc = 0;

	fRegs->intmask.value
		= DesignwareMmcInt {
			.cmdDone = true,
			.dataOver = true
		}.value
		| kDesignwareMmcIntDataError.value
		| kDesignwareMmcIntDataTimeout.value
		| kDesignwareMmcIntCmdError.value;

	dprintf("fRegs->intmask: %#" B_PRIx32 "\n", fRegs->intmask.value);

	fRegs->idsts = 0xffffffff;
 	fRegs->idinten = DWMCI_IDINTEN_MASK;

	fRegs->ctrl = DesignwareMmcCtrl {.intEnable = true};

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "MMC Bus Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/mmc/driver/v1"}},
		{}
	};

	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fMmcBus), attrs, NULL));

	return B_OK;
}


status_t
DesignwareMmcDriver::ExecuteCommand(const mmc_command& cmd, const mmc_data* data)
{
	//dprintf("MmcDriver::ExecuteCommand(%" B_PRIu8 ", %#" B_PRIx32 ")\n", cmd.command, cmd.argument);

	auto PrepareDmaDescs = [this, data]() {
		for (uint32 i = 0; i < data->vecCount; i++) {
			DesignwareMmcIdmacDesc& idmac = fDmaDescs[i];
			generic_io_vec& vec = data->vecs[i];
			//dprintf("  vec[%" B_PRIu32 "]: %#" B_PRIxPHYSADDR ", %#" B_PRIxPHYSADDR "\n", i, vec.base, vec.length);
			idmac.flags = {
				.ld = i == data->vecCount - 1,
				.fs = i == 0,
				.ch = true,
				.own = true
			};
			idmac.cnt = vec.length;
			idmac.addr = vec.base;
			idmac.nextAddr = fDmaDescsPhysAdr + sizeof(DesignwareMmcIdmacDesc)*(i + 1);
		}
	};

	ConditionVariableEntry dataOverCvEntry;

	auto SetupDma = [this, data, &dataOverCvEntry] {
		//dprintf("  fDmaDescsPhysAdr: %#" B_PRIxPHYSADDR "\n", fDmaDescsPhysAdr);

		fDataOverCond.Add(&dataOverCvEntry);

		fRegs->dbaddr = fDmaDescsPhysAdr;

		fRegs->ctrl.dmaEnable = true;

		fRegs->ctrl.dmaReset = true;
		if (retry_count([this] {return (fRegs->ctrl.value & kDesignwareMmcCtrlResetAll.value) == 0;;}, 1000) < B_OK) {
			dprintf("[!] FIFO reset failed\n");
		}
		fRegs->bmod.swReset |= true;

		fRegs->ctrl.useIdmac = true;

		fRegs->bmod.value |= DesignwareMmcDmacBmod{.fb = true, .enable = true}.value;

		fRegs->blksiz = data->blockSize;
		fRegs->bytcnt = data->blockSize * data->blockCnt;

		fRegs->pldmnd = 1;
	};

	if (data != NULL)
		PrepareDmaDescs();

	bigtime_t startTime = system_time();
	CHECK_RET(retry_timeout([this] {return !fRegs->status.busy;}, startTime + 500000));

	if (data != NULL)
		SetupDma();

	bool needInit = fNeedInit;
	if (needInit)
		fNeedInit = false;

	fRegs->cmdarg = cmd.argument;

	ConditionVariableEntry cvEntry;
	fCmdCompletedCond.Add(&cvEntry);

	fRegs->cmd = {
		.indx       = cmd.command,
		.respExp    = cmd.response != NULL,
		.respLong   = cmd.isWideResponse,
		.respCrc    = cmd.doCheckCrc,
		.datExp     = data != NULL,
		.datWr      = data != NULL && data->isWrite,
		.prvDatWait = cmd.command != SD_STOP_TRANSMISSION,
		.stop       = cmd.command == SD_STOP_TRANSMISSION,
		.init       = needInit,
		.useHoldReg = true,
		.start      = true,
	};

	CHECK_RET_MSG(cvEntry.Wait(B_RELATIVE_TIMEOUT, 2000000), "[!] MmcDriver::ExecuteCommand: timeout when executing command\n");

	if (cmd.response != NULL) {
		if (cmd.isWideResponse) {
			cmd.response[3] = fRegs->resp3;
			cmd.response[2] = fRegs->resp2;
			cmd.response[1] = fRegs->resp1;
			cmd.response[0] = fRegs->resp0;
			//dprintf("  -> (%08" B_PRIx32 " %08" B_PRIx32 " %08" B_PRIx32 " %08" B_PRIx32 ")\n", cmd.response[3], cmd.response[2], cmd.response[1], cmd.response[0]);
		} else {
			cmd.response[0] = fRegs->resp0;
			//dprintf("  -> (%#08" B_PRIx32")\n", cmd.response[0]);
		}
	}

	if (data != NULL) {
		CHECK_RET_MSG(dataOverCvEntry.Wait(B_RELATIVE_TIMEOUT, 2000000), "[!] MmcDriver::ExecuteCommand: timeout when transferring data\n");
	}

	return B_OK;
}


int32
DesignwareMmcDriver::HandleInterrupt(void* arg)
{
	return static_cast<DesignwareMmcDriver*>(arg)->HandleInterruptInt();
}


int32
DesignwareMmcDriver::HandleInterruptInt()
{
	//dprintf("DesignwareMmcDriver::HandleInterrupt()\n");

	DesignwareMmcInt ints {.value = fRegs->mintsts.value};
	uint32 idInts = fRegs->idsts;

	//dprintf("  ints: %#" B_PRIx32 "\n", ints.value);
	//dprintf("  idInts: %#" B_PRIx32 "\n", idInts);

	if (ints.value != 0) {
		if (ints.cmdDone) {
			fRegs->rintsts.value
				= DesignwareMmcInt {.cmdDone = true}.value
				| kDesignwareMmcIntCmdError.value;

			status_t res = B_OK;
			if (ints.rto) {
				dprintf("[!] Response timeout.\n");
				res = /*B_TIMED_OUT*/ B_IO_ERROR;
			} else if (ints.respError) {
				dprintf("[!] Response rrror.\n");
				res = B_IO_ERROR;
			} else if (/*cmd.doCheckCrc &&*/ ints.rcrc) {
				dprintf("[!] Response CRC error.\n");
				res = B_IO_ERROR;
			}
			fCmdCompletedCond.NotifyOne(res);
		}

		bool isDataError = (ints.value & (kDesignwareMmcIntDataError.value | kDesignwareMmcIntDataTimeout.value)) != 0;
		if (ints.dataOver || isDataError) {
			fRegs->rintsts.value
				= DesignwareMmcInt {.dataOver = true}.value
				| kDesignwareMmcIntDataError.value
				| kDesignwareMmcIntDataTimeout.value;

			status_t res = B_OK;
			if (isDataError) {
				dprintf("[!] Data error.\n");
				res = B_IO_ERROR;
			}
			fDataOverCond.NotifyOne(res);
		}
	}

	if (idInts != 0) {
		fRegs->idsts = idInts;
	}

	return B_HANDLED_INTERRUPT;
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
	uint32 freq = kilohertz * 1000;
	dprintf("MmcBusImpl::SetClock(%" B_PRIu32 " Hz)\n", freq);
	if (freq == fBase.fClockFreq || freq == 0)
		return B_OK;

	uint64 sclk = fBase.fBusFreq;

	uint32 div = (sclk == freq) ? 0 : div_round_up<uint64>(sclk, 2 * freq);
	dprintf("  div: %" B_PRIu32 "\n", div);

	fBase.fRegs->clkena = 0;
	fBase.fRegs->clksrc = 0;

	fBase.fRegs->clkdiv = div;
	fBase.fRegs->cmd = {
		.prvDatWait = true,
		.updClk = true,
		.start = true
	};
	CHECK_RET(retry_count([this]() {return !fBase.fRegs->cmd.start;}, 10000));

	fBase.fRegs->clkena = DWMCI_CLKEN_ENABLE | DWMCI_CLKEN_LOW_PWR;

	fBase.fRegs->cmd = {
		.prvDatWait = true,
		.updClk = true,
		.start = true
	};
	CHECK_RET(retry_count([this]() {return !fBase.fRegs->cmd.start;}, 10000));

	fBase.fClockFreq = freq;
	return B_OK;
}


status_t
DesignwareMmcDriver::MmcBusImpl::ExecuteCommand(uint8 command, uint32 argument, uint32* result)
{
	mmc_command cmd {
		.command = command,
		.argument = argument,
		.isWideResponse = command == SD_ALL_SEND_CID || command == SD_SEND_CSD,
		.response = result
	};
	return fBase.ExecuteCommand(cmd, NULL);
}


status_t
DesignwareMmcDriver::MmcBusImpl::SetBusWidth(int width)
{
	dprintf("MmcBusImpl::SetBusWidth(%d)\n", width);

	switch (width) {
	case 8:
		fBase.fRegs->ctype = DesignwareMmcCardType::bit8;
		break;
	case 4:
		fBase.fRegs->ctype = DesignwareMmcCardType::bit4;
		break;
	default:
		fBase.fRegs->ctype = DesignwareMmcCardType::bit1;
		break;
	}

	uint32 uhsReg = fBase.fRegs->uhsReg;
	if (fBase.fDdrMode)
		uhsReg |= DWMCI_DDR_MODE;
	else
		uhsReg |= ~DWMCI_DDR_MODE;
	fBase.fRegs->uhsReg = uhsReg;

	return B_OK;
}


status_t
DesignwareMmcDriver::MmcBusImpl::ExecuteCommand(const mmc_command& cmd, const mmc_data* data)
{
	return fBase.ExecuteCommand(cmd, data);
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
