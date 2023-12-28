#pragma once

#include <SupportDefs.h>


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
};

static const DesignwareMmcCtrl kDesignwareMmcCtrlResetAll = {
	.reset = true,
	.fifoReset = true,
	.dmaReset = true,
};

union DesignwareMmcClkEna {
	struct {
		uint32 enable:    1; //  0
		uint32 unknown1: 15; //  1
		uint32 lowPwr:    1; // 16
		uint32 unknown2: 15; // 17
	};
	uint32 value;
};

enum class DesignwareMmcCardType: uint32 {
	bit1 = 0,
	bit4 = 1 << 0,
	bit8 = 1 << 16
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
};

union DesignwareMmcDmacUhsReg {
	struct {
		uint32 unknown1: 16; //  0
		uint32 ddrMode:   1; // 16
		uint32 unknown2: 15; // 17
	};
	uint32 value;
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
};

union DesignwareMmcIdIntEn {
	struct {
		uint32 ti:        1; //  0
		uint32 ri:        1; //  1
		uint32 unknown1:  6; //  2
		uint32 ni:        5; //  8
		uint32 unknown2: 23; //  9
	};
	uint32 value;
};

struct DesignwareMmcRegs {
	DesignwareMmcCtrl ctrl;
	uint32 pwren;
	uint32 clkdiv;
	uint32 clksrc;
	DesignwareMmcClkEna clkena;
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
	DesignwareMmcDmacUhsReg uhsReg;
	uint32 unknown1[2];
	DesignwareMmcDmacBmod bmod;
	uint32 pldmnd;
	union {
		struct {
			uint32 dbaddr;
			uint32 idsts;
			DesignwareMmcIdIntEn idinten;
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
