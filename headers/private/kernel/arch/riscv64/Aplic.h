#pragma once

#include <SupportDefs.h>
#include <stddef.h>
#include <assert.h>


enum class AplicDeliveryMode: uint32 {
	direct = 0,
	msi = 1,
};

enum class AplicSourceMode: uint32 {
	inactive = 0,
	detached = 1,
	edge1 = 4,
	edge0 = 5,
	level1 = 6,
	level0 = 7,
};

union AplicDomainCfg {
	struct {
		uint32 be: 1; // big endian
		uint32 reserved1: 1;
		AplicDeliveryMode dm: 1;
		uint32 reserved2: 4;
		uint32 const1: 1 = 0;
		uint32 ie: 1; // interrupt enable
		uint32 reserved3: 15;
		uint32 const2: 8 = 0x80;
	};
	uint32 val;
};

union AplicSourceCfg {
	struct {
		uint32 unused1: 10;
		uint32 d: 1; // delegate
		uint32 reserved1: 21;
	};
	struct {
		AplicSourceMode sm: 3;
		uint32 reserved1: 7;
		uint32 d: 1 = 0;
		uint32 reserved2: 21;
	} non_deleg;
	struct {
		uint32 childIdx: 10;
		uint32 d: 1 = 1;
		uint32 reserved2: 21;
	} deleg;
	uint32 val;
};

union AplicTarget {
	struct {
		uint32 iprio: 8; // interrupt priority
		uint32 reserved1: 10;
		uint32 hartIdx: 14;
	} direct;
	struct {
		uint32 eiid: 11;
		uint32 reserved1: 1;
		uint32 guestIdx: 6;
		uint32 hartIdx: 14;
	} msi;
	uint32 val;
};

union AplicGenMsi {
	struct {
		uint32 eiid: 11; // External Interrupt Identity
		uint32 reserved1: 1;
		uint32 busy: 1;
		uint32 reserved2: 5;
		uint32 hartIdx: 14;
	};
	uint32 val;
};

union AplicTopi {
	struct {
		uint32 prio: 8;
		uint32 reserved1: 8;
		uint32 intNo: 10;
		uint32 reserved2: 6;
	};
	uint32 val;
};

struct AplicIdc {
	uint32 idelivery;
	uint32 iforce;
	uint32 ithreshold;
	uint32 reserved1[3];
	AplicTopi topi;
	AplicTopi claimi;
};

struct AplicRegs {
	union {
		AplicDomainCfg domainCfg;
		AplicSourceCfg sourceCfg[1024];
	};
	uint32 reserved1[752];
	uint32 mMsiAddrCfgLo;
	uint32 mMsiAddrCfgHi;
	uint32 sMsiAddrCfgLo;
	uint32 sMsiAddrCfgHi;
	uint32 reserved2[12];

	uint32 setIp[32];
	uint32 reserved3[23];
	uint32 setIpNum;
	uint32 reserved4[8];

	uint32 clrIp[32];
	uint32 reserved5[23];
	uint32 clrIpNum;
	uint32 reserved6[8];

	uint32 setIe[32];
	uint32 reserved7[23];
	uint32 setIeNum;
	uint32 reserved8[8];

	uint32 clrIe[32];
	uint32 reserved9[23];
	uint32 clrIeNum;
	uint32 reserved10[8];

	uint32 setIpNumLe;
	uint32 setIpNumBe;
	uint32 reserved11[1022];
	union {
		AplicGenMsi genMsi;
		AplicTarget target[1024];
	};
	AplicIdc idc[];
};


static_assert(offsetof(AplicRegs, domainCfg)     ==      0);
static_assert(offsetof(AplicRegs, mMsiAddrCfgLo) == 0x1BC0);
static_assert(offsetof(AplicRegs, setIp)         == 0x1C00);
static_assert(offsetof(AplicRegs, setIpNum)      == 0x1CDC);
static_assert(offsetof(AplicRegs, clrIp)         == 0x1D00);
static_assert(offsetof(AplicRegs, clrIpNum)      == 0x1DDC);
static_assert(offsetof(AplicRegs, setIe)         == 0x1E00);
static_assert(offsetof(AplicRegs, setIeNum)      == 0x1EDC);
static_assert(offsetof(AplicRegs, clrIe)         == 0x1F00);
static_assert(offsetof(AplicRegs, clrIeNum)      == 0x1FDC);
static_assert(offsetof(AplicRegs, setIpNumLe)    == 0x2000);
static_assert(offsetof(AplicRegs, genMsi)        == 0x3000);
