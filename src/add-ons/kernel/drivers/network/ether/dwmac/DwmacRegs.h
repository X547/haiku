#pragma once

#include <SupportDefs.h>
#include <assert.h>

struct DwmacMacL3l4Regs {
	uint32 ctrl;
	uint32 l4Addr;
	uint32 unknown1[2];
	uint32 l3Addr0;
	uint32 l3Addr1;
	uint32 unknown2[6];
};

static_assert(offsetof(DwmacMacL3l4Regs, ctrl)    == 0x00);
static_assert(offsetof(DwmacMacL3l4Regs, l4Addr)  == 0x04);
static_assert(offsetof(DwmacMacL3l4Regs, l3Addr0) == 0x10);
static_assert(offsetof(DwmacMacL3l4Regs, l3Addr1) == 0x14);
static_assert(sizeof  (DwmacMacL3l4Regs)          == 0x30);

union DwmacMacConfig {
	struct {
		uint32 re: 1;
		uint32 te: 1;
		uint32 unknown1: 10;
		uint32 lm: 1;
		uint32 dm: 1;
		uint32 fes: 1;
		uint32 ps: 1;
		uint32 je: 1;
		uint32 jd: 1;
		uint32 unknown2: 1;
		uint32 wd: 1;
		uint32 acs: 1;
		uint32 cst: 1;
		uint32 unknown3: 1;
		uint32 gpslce: 1;
		uint32 unknown4: 8;
	};
	uint32 val;
};

union DwmacQxTxFlowCtrl {
	struct {
		uint32 unknown1: 1;
		uint32 tfe: 1;
		uint32 unknown2: 14;
		uint32 pt: 16;
	};
	uint32 val;
};

union DwmacRxFlowCtrl {
	struct {
		uint32 rfe: 1;
		uint32 unknown1: 31;
	};
	uint32 val;
};

union DwmacTxqPrtyMap0 {
	struct {
		uint32 pstq0: 8;
		uint32 unknown1: 24;
	};
	uint32 val;
};

enum class DwmacRxqCtrl0Rxq0en: uint32 {
	notEnabled = 0,
	enabledAv = 1,
	enabledDsb = 2,
};

union DwmacRxqCtrl0 {
	struct {
		DwmacRxqCtrl0Rxq0en rxq0en: 2;
		uint32 unknown1: 30;
	};
	uint32 val;
};

union DwmacRxqCtrl2 {
	struct {
		uint32 psrq0: 8;
		uint32 unknown1: 24;
	};
	uint32 val;
};

union DwmacHwFeature1 {
	struct {
		uint32 rxFifoSize: 5;
		uint32 unknown1: 1;
		uint32 txFifoSize: 5;
		uint32 unknown2: 21;
	};
	uint32 val;
};

enum class DwmacMdioAddrGoc: uint32 {
	write = 1,
	read  = 3,
};

enum class DwmacMdioAddrCr: uint32 {
	cr20_35   = 2,
	cr250_300 = 5,
};

union DwmacMdioAddr {
	struct {
		uint32 gb: 1;
		uint32 c45e: 1;
		DwmacMdioAddrGoc goc: 2;
		uint32 skap: 1;
		uint32 unknown1: 3;
		DwmacMdioAddrCr cr: 3;
		uint32 unknown2: 5;
		uint32 rda: 5;
		uint32 pa: 11;
	};
	uint32 val;
};

union DwmacMdioData {
	struct {
		uint32 gd: 16;
		uint32 unknown1: 16;
	};
	uint32 val;
};

struct DwmacMacRegs {
	DwmacMacConfig config;
	uint32 extConfig;
	uint32 packetFilter;
	uint32 unknown1;
	uint32 hashTab[16];
	uint32 vlanTag;
	uint32 vlanTagData;
	uint32 vlanHashTable;
	uint32 unknown2;
	uint32 vlanCtrl;
	uint32 unknown3[3];
	DwmacQxTxFlowCtrl qxTxFlowCtrl[8];
	DwmacRxFlowCtrl rxFlowCtrl;
	uint32 unknown4;
	DwmacTxqPrtyMap0 txqPrtyMap0;
	uint32 txqPrtyMap1;
	DwmacRxqCtrl0 rxqCtrl0;
	uint32 rxqCtrl1;
	DwmacRxqCtrl2 rxqCtrl2;
	uint32 rxqCtrl3;
	uint32 intStatus;
	uint32 intEn;
	uint32 unknown5[2];
	uint32 pmt;
	uint32 unknown6[6];
	uint32 usTicCounter;
	uint32 pcsBase;
	uint32 unknown7[5];
	uint32 phyifControlStatus;
	uint32 unknown8[6];
	uint32 debug;
	uint32 unknown9;
	uint32 hwFeature0;
	DwmacHwFeature1 hwFeature1;
	uint32 hwFeature2;
	uint32 hwFeature3;
	uint32 unknown10[53];
	DwmacMdioAddr mdioAddr;
	DwmacMdioData mdioData;
	uint32 unknown11;
	uint32 gpioStatus;
	uint32 arpAddr;
	uint32 unknown12[59];
	struct {
		uint32 hi;
		uint32 lo;
	} addr[192];
	DwmacMacL3l4Regs l3l4[11];
	uint32 unknown13[4];
	uint32 timestampStatus;
	uint32 unknown14[119];
};

static_assert(offsetof(DwmacMacRegs, config)             == 0x000);
static_assert(offsetof(DwmacMacRegs, extConfig)          == 0x004);
static_assert(offsetof(DwmacMacRegs, packetFilter)       == 0x008);
static_assert(offsetof(DwmacMacRegs, hashTab)            == 0x010);
static_assert(offsetof(DwmacMacRegs, vlanTag)            == 0x050);
static_assert(offsetof(DwmacMacRegs, vlanTagData)        == 0x054);
static_assert(offsetof(DwmacMacRegs, vlanHashTable)      == 0x058);
static_assert(offsetof(DwmacMacRegs, vlanCtrl)           == 0x060);
static_assert(offsetof(DwmacMacRegs, qxTxFlowCtrl)       == 0x070);
static_assert(offsetof(DwmacMacRegs, rxFlowCtrl)         == 0x090);
static_assert(offsetof(DwmacMacRegs, txqPrtyMap0)        == 0x098);
static_assert(offsetof(DwmacMacRegs, txqPrtyMap1)        == 0x09C);
static_assert(offsetof(DwmacMacRegs, rxqCtrl0)           == 0x0a0);
static_assert(offsetof(DwmacMacRegs, rxqCtrl1)           == 0x0a4);
static_assert(offsetof(DwmacMacRegs, rxqCtrl2)           == 0x0a8);
static_assert(offsetof(DwmacMacRegs, rxqCtrl3)           == 0x0ac);
static_assert(offsetof(DwmacMacRegs, intStatus)          == 0x0b0);
static_assert(offsetof(DwmacMacRegs, intEn)              == 0x0b4);
static_assert(offsetof(DwmacMacRegs, pmt)                == 0x0c0);
static_assert(offsetof(DwmacMacRegs, usTicCounter)       == 0x0dc);
static_assert(offsetof(DwmacMacRegs, pcsBase)            == 0x0e0);
static_assert(offsetof(DwmacMacRegs, phyifControlStatus) == 0x0f8);
static_assert(offsetof(DwmacMacRegs, debug)              == 0x114);
static_assert(offsetof(DwmacMacRegs, hwFeature0)         == 0x11c);
static_assert(offsetof(DwmacMacRegs, hwFeature1)         == 0x120);
static_assert(offsetof(DwmacMacRegs, hwFeature2)         == 0x124);
static_assert(offsetof(DwmacMacRegs, hwFeature3)         == 0x128);
static_assert(offsetof(DwmacMacRegs, mdioAddr)           == 0x200);
static_assert(offsetof(DwmacMacRegs, mdioData)           == 0x204);
static_assert(offsetof(DwmacMacRegs, gpioStatus)         == 0x20C);
static_assert(offsetof(DwmacMacRegs, arpAddr)            == 0x210);
static_assert(offsetof(DwmacMacRegs, addr)               == 0x300);
static_assert(offsetof(DwmacMacRegs, l3l4)               == 0x900);
static_assert(offsetof(DwmacMacRegs, timestampStatus)    == 0xb20);
static_assert(sizeof  (DwmacMacRegs)                     == 0xd00);


union DwmacMtlTxOpMode {
	uint32 val;
};

union DwmacMtlTxDebug {
	uint32 val;
};

union DwmacMtlRxOpMode {
	uint32 val;
};

union DwmacMtlRxDebug {
	uint32 val;
};

struct DwmacMtlChannelRegs {
	DwmacMtlTxOpMode txOpMode;
	uint32 unknown1;
	DwmacMtlTxDebug txDebug;
	uint32 unknown2;
	uint32 etsCtrl;
	uint32 unknown3;
	uint32 txqWeight;
	uint32 sendSlpCred;
	uint32 highCred;
	uint32 lowCred;
	uint32 unknown4;
	uint32 intCtrl;
	DwmacMtlRxOpMode rxOpMode;
	uint32 unknown5;
	DwmacMtlRxDebug rxDebug;
	uint32 unknown6;
};

static_assert(offsetof(DwmacMtlChannelRegs, txOpMode)    == 0x00);
static_assert(offsetof(DwmacMtlChannelRegs, txDebug)     == 0x08);
static_assert(offsetof(DwmacMtlChannelRegs, etsCtrl)     == 0x10);
static_assert(offsetof(DwmacMtlChannelRegs, txqWeight)   == 0x18);
static_assert(offsetof(DwmacMtlChannelRegs, sendSlpCred) == 0x1c);
static_assert(offsetof(DwmacMtlChannelRegs, highCred)    == 0x20);
static_assert(offsetof(DwmacMtlChannelRegs, lowCred)     == 0x24);
static_assert(offsetof(DwmacMtlChannelRegs, intCtrl)     == 0x2c);
static_assert(offsetof(DwmacMtlChannelRegs, rxOpMode)    == 0x30);
static_assert(offsetof(DwmacMtlChannelRegs, rxDebug)     == 0x38);
static_assert(sizeof  (DwmacMtlChannelRegs)              == 0x40);


struct DwmacMtlRegs {
	DwmacMtlChannelRegs chan[12];
};

static_assert(sizeof(DwmacMtlRegs) == 0x300);


union DwmacDmaChannelControl {
	struct {
		uint32 unknown1: 16;
		uint32 pblx8: 1;
		uint32 unknown2: 1;
		uint32 dsl: 14;
	};
	uint32 val;
};

union DwmacDmaChannelTxControl {
	struct {
		uint32 st: 1;
		uint32 unknown1: 3;
		uint32 osp: 1;
		uint32 unknown2: 11;
		uint32 txpbl: 6;
		uint32 unknown3: 6;
	};
	uint32 val;
};

union DwmacDmaChannelRxControl {
	struct {
		uint32 sl: 1;
		uint32 rbsz: 14;
		uint32 unknown1: 1;
		uint32 rxpbl: 6;
		uint32 unknown2: 10;
	};
	uint32 val;
};

struct DwmacDmaChannelRegs {
	DwmacDmaChannelControl control;
	DwmacDmaChannelTxControl txControl;
	DwmacDmaChannelRxControl rxControl;
	uint32 unknown1;
	uint32 txBaseAddrHi;
	uint32 txBaseAddrLo;
	uint32 rxBaseAddrHi;
	uint32 rxBaseAddrLo;
	uint32 txEndAddr;
	uint32 unknown2;
	uint32 rxEndAddr;
	uint32 txRingLen;
	uint32 rxRingLen;
	uint32 intrEna;
	uint32 rxWatchdog;
	uint32 slotCtrlStatus;
	uint32 unknown3;
	uint32 curTxDesc;
	uint32 unknown4;
	uint32 curRxDesc;
	uint32 unknown5;
	uint32 curTxBufAddr;
	uint32 unknown6;
	uint32 curRxBufAddr;
	uint32 status;
	uint32 unknown7[7];
};

static_assert(offsetof(DwmacDmaChannelRegs, control)        ==  0x0);
static_assert(offsetof(DwmacDmaChannelRegs, txControl)      ==  0x4);
static_assert(offsetof(DwmacDmaChannelRegs, rxControl)      ==  0x8);
static_assert(offsetof(DwmacDmaChannelRegs, txBaseAddrHi)   == 0x10);
static_assert(offsetof(DwmacDmaChannelRegs, txBaseAddrLo)   == 0x14);
static_assert(offsetof(DwmacDmaChannelRegs, rxBaseAddrHi)   == 0x18);
static_assert(offsetof(DwmacDmaChannelRegs, rxBaseAddrLo)   == 0x1c);
static_assert(offsetof(DwmacDmaChannelRegs, txEndAddr)      == 0x20);
static_assert(offsetof(DwmacDmaChannelRegs, rxEndAddr)      == 0x28);
static_assert(offsetof(DwmacDmaChannelRegs, txRingLen)      == 0x2c);
static_assert(offsetof(DwmacDmaChannelRegs, rxRingLen)      == 0x30);
static_assert(offsetof(DwmacDmaChannelRegs, intrEna)        == 0x34);
static_assert(offsetof(DwmacDmaChannelRegs, rxWatchdog)     == 0x38);
static_assert(offsetof(DwmacDmaChannelRegs, slotCtrlStatus) == 0x3c);
static_assert(offsetof(DwmacDmaChannelRegs, curTxDesc)      == 0x44);
static_assert(offsetof(DwmacDmaChannelRegs, curRxDesc)      == 0x4c);
static_assert(offsetof(DwmacDmaChannelRegs, curTxBufAddr)   == 0x54);
static_assert(offsetof(DwmacDmaChannelRegs, curRxBufAddr)   == 0x5c);
static_assert(offsetof(DwmacDmaChannelRegs, status)         == 0x60);
static_assert(sizeof  (DwmacDmaChannelRegs)                 == 0x80);


union DwmacDmaBusMode {
	struct {
		uint32 swr: 1;
		uint32 unknown1: 31;
	};
	uint32 val;
};

union DwmacDmaSysBusMode {
	struct {
		uint32 unknown1: 1;
		uint32 blen4: 1;
		uint32 blen8: 1;
		uint32 blen16: 1;
		uint32 unknown2: 7;
		uint32 eame: 1;
		uint32 unknown3: 4;
		uint32 rdOsrLmt: 4;
		uint32 unknown4: 12;
	};
	uint32 val;
};

struct DwmacDmaRegs {
	DwmacDmaBusMode busMode;
	DwmacDmaSysBusMode sysBusMode;
	uint32 status;
	uint32 debugStatus0;
	uint32 debugStatus1;
	uint32 debugStatus2;
	uint32 unknown1[4];
	uint32 axiBusMode;
	uint32 unknown2[9];
	uint32 tbsCtrl;
	uint32 unknown3[43];
	DwmacDmaChannelRegs channels[8];
};

static_assert(offsetof(DwmacDmaRegs, busMode)      ==   0x0);
static_assert(offsetof(DwmacDmaRegs, sysBusMode)   ==   0x4);
static_assert(offsetof(DwmacDmaRegs, status)       ==   0x8);
static_assert(offsetof(DwmacDmaRegs, debugStatus0) ==   0xc);
static_assert(offsetof(DwmacDmaRegs, debugStatus1) ==  0x10);
static_assert(offsetof(DwmacDmaRegs, debugStatus2) ==  0x14);
static_assert(offsetof(DwmacDmaRegs, axiBusMode)   ==  0x28);
static_assert(offsetof(DwmacDmaRegs, tbsCtrl)      ==  0x50);
static_assert(offsetof(DwmacDmaRegs, channels)     == 0x100);

struct DwmacRegs {
	DwmacMacRegs mac;
	DwmacMtlRegs mtl;
	DwmacDmaRegs dma;
};

static_assert(offsetof(DwmacRegs, mac) == 0x0000);
static_assert(offsetof(DwmacRegs, mtl) == 0x0d00);
static_assert(offsetof(DwmacRegs, dma) == 0x1000);


union DwmacDescDes3 {
	struct {
		uint32 length: 15;
		uint32 unknown1: 9;
		uint32 buf1v: 1;
		uint32 unknown2: 3;
		uint32 ld: 1;
		uint32 fd: 1;
		uint32 unknown3: 1;
		uint32 own: 1;
	};
	uint32 val;
};

struct DwmacDesc {
	uint32 des0;
	uint32 des1;
	uint32 des2;
	DwmacDescDes3 des3;
};
