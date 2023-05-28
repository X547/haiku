/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _PCICONTROLLERPLDA_H_
#define _PCICONTROLLERPLDA_H_

#include <bus/PCI.h>
#include <arch/generic/generic_int.h>
#include <arch/generic/msi.h>

#include <AutoDeleterOS.h>
#include <lock.h>

#include <stddef.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define PLDA_PCI_DRIVER_MODULE_NAME "busses/pci/plda/driver_v1"


enum {
	fdtPciRangeConfig      = 0x00000000,
	fdtPciRangeIoPort      = 0x01000000,
	fdtPciRangeMmio32Bit   = 0x02000000,
	fdtPciRangeMmio64Bit   = 0x03000000,
	fdtPciRangeTypeMask    = 0x03000000,
	fdtPciRangeAliased     = 0x20000000,
	fdtPciRangePrefechable = 0x40000000,
	fdtPciRangeRelocatable = 0x80000000,
};


union PciAddress {
	struct {
		uint32 offset: 8;
		uint32 function: 3;
		uint32 device: 5;
		uint32 bus: 8;
		uint32 unused: 8;
	};
	uint32 val;
};

union PciAddressEcam {
	struct {
		uint32 offset: 12;
		uint32 function: 3;
		uint32 device: 5;
		uint32 bus: 8;
		uint32 unused: 4;
	};
	uint32 val;
};

struct RegisterRange {
	phys_addr_t parentBase;
	phys_addr_t childBase;
	uint64 size;
};

struct InterruptMapMask {
	uint32_t childAdr;
	uint32_t childIrq;
};

struct InterruptMap {
	uint32_t childAdr;
	uint32_t childIrq;
	uint32_t parentIrqCtrl;
	uint32_t parentIrq;
};


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


class MsiInterruptCtrlPlda: public InterruptSource, public MSIInterface {
public:
			virtual				~MsiInterruptCtrlPlda() = default;

			status_t			Init(PciPldaRegs volatile* dbiRegs, int32 msiIrq);

			status_t			AllocateVectors(uint8 count, uint8& startVector, uint64& address,
									uint16& data) final;
			void				FreeVectors(uint8 count, uint8 startVector) final;

			void				EnableIoInterrupt(int vector) final;
			void				DisableIoInterrupt(int vector) final;
			void				ConfigureIoInterrupt(int vector, uint32 config) final;
			void				EndOfInterrupt(int vector) final;
			int32				AssignToCpu(int32 vector, int32 cpu) final;

private:
	static	int32				InterruptReceived(void* arg);
	inline	int32				InterruptReceivedInt();

private:
			PciPldaRegs volatile* fRegs {};

			uint32				fAllocatedMsiIrqs[1];
			phys_addr_t			fMsiPhysAddr {};
			long				fMsiStartIrq {};
			uint64				fMsiData {};
};


class PciControllerPlda {
public:
	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, PciControllerPlda*& outDriver);
	void UninitDriver();

	status_t ReadConfig(
				uint8 bus, uint8 device, uint8 function,
				uint16 offset, uint8 size, uint32 &value);

	status_t WriteConfig(
				uint8 bus, uint8 device, uint8 function,
				uint16 offset, uint8 size, uint32 value);

	status_t GetMaxBusDevices(int32& count);

	status_t ReadIrq(
				uint8 bus, uint8 device, uint8 function,
				uint8 pin, uint8& irq);

	status_t WriteIrq(
				uint8 bus, uint8 device, uint8 function,
				uint8 pin, uint8 irq);

	status_t GetRange(uint32 index, pci_resource_range* range);

	status_t Finalize();

	MSIInterface* GetMsiDriver() {return static_cast<MSIInterface*>(&fIrqCtrl);}

private:
	status_t ReadResourceInfo();
	inline status_t InitDriverInt(device_node* node);

	void SetAtrEntry(uint32 index, phys_addr_t srcAddr, phys_addr_t trslAddr,
	size_t windowSize, PciPldaAtrTrslParam trslParam);

	inline addr_t ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset);

	PciPldaRegs volatile* GetRegs() {return fRegs;}

	phys_addr_t AllocRegister(uint32 kind, size_t size);
	InterruptMap* LookupInterruptMap(uint32 childAdr, uint32 childIrq);
	uint32 GetPciBarKind(uint32 val);
	void GetBarValMask(uint32& val, uint32& mask, uint8 bus, uint8 device, uint8 function, uint16 offset);
	void GetBarKindValSize(uint32& barKind, uint64& val, uint64& size, uint8 bus, uint8 device, uint8 function, uint16 offset);
	uint64 GetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset);
	void SetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 barKind, uint64 val);
	bool AllocBar(uint8 bus, uint8 device, uint8 function, uint16 offset);
	void AllocRegsForDevice(uint8 bus, uint8 device, uint8 function);

private:
	spinlock fLock = B_SPINLOCK_INITIALIZER;

	device_node* fNode {};

	AreaDeleter fConfigArea;
	addr_t fConfigPhysBase {};
	addr_t fConfigBase {};
	size_t fConfigSize {};

	pci_resource_range fResourceRanges[kPciRangeEnd] {};
	phys_addr_t fResourceFree[kPciRangeEnd] {};
	InterruptMapMask fInterruptMapMask {};
	uint32 fInterruptMapLen {};
	ArrayDeleter<InterruptMap> fInterruptMap;

	AreaDeleter fRegsArea;
	addr_t fRegsPhysBase {};
	PciPldaRegs volatile* fRegs {};
	size_t fRegsSize {};

	MsiInterruptCtrlPlda fIrqCtrl;
};


extern device_manager_info* gDeviceManager;
extern pci_module_info* gPCI;

#endif	// _PCICONTROLLERPLDA_H_
