#pragma once


enum {
	kPciAtuOffset = 0x300000,
};

enum {
	kPciAtuOutbound  = 0,
	kPciAtuInbound   = 1,
};

enum {
	// ctrl1
	kPciAtuTypeMem  = 0,
	kPciAtuTypeIo   = 2,
	kPciAtuTypeCfg0 = 4,
	kPciAtuTypeCfg1 = 5,
	// ctrl2
	kPciAtuBarModeEnable = 1 << 30,
	kPciAtuEnable        = 1 << 31,
};

enum {
	PLDA_EP_ENABLE = 0,
	PLDA_RP_ENABLE = 1,

	PLDA_LINK_UP   = 1,
	PLDA_LINK_DOWN = 0,

	IDS_REVISION_ID       =     0x02,
	IDS_PCI_TO_PCI_BRIDGE = 0x060400,
	IDS_CLASS_CODE_SHIFT  =        8,
};

enum {
	PLDA_DATA_LINK_ACTIVE      = 1 <<  5,
	PREF_MEM_WIN_64_SUPPORT    = 1 <<  3,
	PMSG_LTR_SUPPORT           = 1 <<  2,
	PDLA_LINK_SPEED_GEN2       = 1 << 12,
	PLDA_FUNCTION_DIS          = 1 << 15,
	PLDA_FUNC_NUM              = 4,
	PLDA_PHY_FUNC_SHIFT        = 9,
	PHY_KVCO_FINE_TUNE_LEVEL   = 0x91,
	PHY_KVCO_FINE_TUNE_SIGNALS =  0xc,
};

enum {
	STG_SYSCON_K_RP_NEP_SHIFT         =      0x8,
	STG_SYSCON_K_RP_NEP_MASK          =    0x100,
	STG_SYSCON_AXI4_SLVL_ARFUNC_MASK  = 0x7FFF00,
	STG_SYSCON_AXI4_SLVL_ARFUNC_SHIFT =      0x8,
	STG_SYSCON_AXI4_SLVL_AWFUNC_MASK  =   0x7FFF,
	STG_SYSCON_AXI4_SLVL_AWFUNC_SHIFT =      0x0,
	STG_SYSCON_CLKREQ_SHIFT           =     0x16,
	STG_SYSCON_CLKREQ_MASK            = 0x400000,
	STG_SYSCON_CKREF_SRC_SHIFT        =     0x12,
	STG_SYSCON_CKREF_SRC_MASK         =  0xC0000,
};


enum class PciPldaAtrTrslId: uint32 {
	memory = 0,
	config = 1,
};

union PciPldaAtrTrslParam {
	struct {
		PciPldaAtrTrslId type: 1;
		uint32 unknown1: 21;
		uint32 dir: 1;
		uint32 unknown2: 9;
	};
	uint32 val;
};

union PciPldaAtrAddrLow {
	struct {
		uint32 enable: 1;
		uint32 windowSize: 6;
		uint32 reserved: 5;
		uint32 address: 20;
	};
	uint32 val;
};

struct PciPldaAtr {
	PciPldaAtrAddrLow srcAddrLow;
	uint32 srcAddrHigh;
	uint32 trslAddrLow;
	uint32 trslAddrHigh;
	PciPldaAtrTrslParam trslParam;
	uint32 unknown1[3];
};

union PciPldaInt {
	struct {
		uint32 unknown1: 16;

		uint32 axiPostError: 1;
		uint32 axiFetchError: 1;
		uint32 axiDiscardError: 1;
		uint32 unknown2: 1;

		uint32 pciePostError: 1;
		uint32 pcieFetchError: 1;
		uint32 pcieDiscardError: 1;
		uint32 unknown3: 1;

		uint32 a: 1;
		uint32 b: 1;
		uint32 c: 1;
		uint32 d: 1;
		uint32 msi: 1;

		uint32 unknown4: 3;
	};
	uint32 val;
};

static constexpr PciPldaInt kPciPldaIntErrors = {
	.axiPostError = true,
	.axiFetchError = true,
	.axiDiscardError = true,
	.pciePostError = true,
	.pcieFetchError = true,
	.pcieDiscardError = true
};

static constexpr PciPldaInt kPciPldaIntLegacy = {
	.a = true,
	.b = true,
	.c = true,
	.d = true
};

static constexpr PciPldaInt kPciPldaIntAll = {
	.axiPostError = true,
	.axiFetchError = true,
	.axiDiscardError = true,
	.pciePostError = true,
	.pcieFetchError = true,
	.pcieDiscardError = true,
	.a = true,
	.b = true,
	.c = true,
	.d = true,
	.msi = true
};

struct PciPldaRegs {
	uint32 unknown1[6];
	uint32 pcieBasicStatus;
	uint32 unknown2[25];
	uint32 genSettings;
	uint32 unknown3[6];
	uint32 pciePciIds;
	uint32 unknown4[5];
	uint32 pciMisc;
	uint32 unknown5[17];
	uint32 pcieWinrom;
	uint32 unknown6[16];
	uint32 pcieCfgnum;
	uint32 unknown7[15];
	PciPldaInt imaskLocal;
	PciPldaInt istatusLocal;
	uint32 unknown8[2];
	uint32 imsiAddr;
	uint32 istatusMsi;
	uint32 unknown9[150];
	uint32 pmsgSupportRx;
	uint32 unknown10[259];
	PciPldaAtr xr3pciAtrAxi4Slv0[8];
	uint32 unknown11[448];
	uint32 cfgSpace;
};

static_assert(offsetof(PciPldaRegs, pcieBasicStatus) ==   0x018);
static_assert(offsetof(PciPldaRegs, genSettings) ==        0x80);
static_assert(offsetof(PciPldaRegs, pciePciIds) ==         0x9C);
static_assert(offsetof(PciPldaRegs, pciMisc) ==            0xB4);
static_assert(offsetof(PciPldaRegs, pcieWinrom) ==         0xFC);
static_assert(offsetof(PciPldaRegs, pcieCfgnum) ==        0x140);
static_assert(offsetof(PciPldaRegs, imaskLocal) ==        0x180);
static_assert(offsetof(PciPldaRegs, istatusLocal) ==      0x184);
static_assert(offsetof(PciPldaRegs, imsiAddr) ==          0x190);
static_assert(offsetof(PciPldaRegs, istatusMsi) ==        0x194);
static_assert(offsetof(PciPldaRegs, pmsgSupportRx) ==     0x3F0);
static_assert(offsetof(PciPldaRegs, xr3pciAtrAxi4Slv0) == 0x800);
static_assert(offsetof(PciPldaRegs, cfgSpace) ==         0x1000);
