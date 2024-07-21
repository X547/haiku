#pragma once

#include <dm2/bus/FDT.h>
#include <dm2/bus/PCI.h>
#include <arch/generic/generic_int.h>
#include <arch/generic/msi.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <util/Vector.h>

#include "PldaRegs.h"


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define CHECK_RET_MSG(err, msg...) \
	{ \
		status_t _err = (err); \
		if (_err < B_OK) { \
			dprintf(msg); \
			return _err; \
		} \
	} \


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
	uint32 childAdr;
	uint32 childIrq;
};

struct InterruptMap {
	uint32 childAdr;
	uint32 childIrq;
	uint32 parentIrqCtrl;
	uint32 parentIrq;
};

struct PciBarKind {
	uint8 type;
	uint8 address_type;
};


class MsiInterruptCtrlPlda: public InterruptSource, public MSIInterface {
public:
			virtual				~MsiInterruptCtrlPlda() = default;

			status_t			Init(PciPldaRegs volatile* regs, int32 msiIrq);

			status_t			AllocateVectors(uint32 count, uint32& startVector, uint64& address,
									uint32& data) final;
			void				FreeVectors(uint32 count, uint32 startVector) final;

			void				EnableIoInterrupt(int vector) final;
			void				DisableIoInterrupt(int vector) final;
			void				ConfigureIoInterrupt(int vector, uint32 config) final;
			int32				AssignToCpu(int32 vector, int32 cpu) final;

private:
	static	int32				InterruptReceived(void* arg);
	inline	int32				InterruptReceivedInt();

private:
			PciPldaRegs volatile* fRegs {};

			uint32				fAllocatedMsiIrqs[1];
			phys_addr_t			fMsiPhysAddr {};
			int32				fMsiStartIrq {};
			uint64				fMsiData {};
};


class PciControllerPlda: public DeviceDriver {
public:
	PciControllerPlda(DeviceNode* node): fNode(node), fPciCtrl(*this) {}
	virtual ~PciControllerPlda() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

	status_t ReadResourceInfo();

	void SetAtrEntry(uint32 index, phys_addr_t srcAddr, phys_addr_t trslAddr,
		size_t windowSize, PciPldaAtrTrslParam trslParam);

	inline addr_t ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset);

	phys_addr_t AllocRegister(PciBarKind kind, size_t size);
	InterruptMap* LookupInterruptMap(uint32 childAdr, uint32 childIrq);
	PciBarKind GetPciBarKind(uint32 val);
	void GetBarValMask(uint32& val, uint32& mask, uint8 bus, uint8 device, uint8 function, uint16 offset);
	void GetBarKindValSize(PciBarKind& barKind, uint64& val, uint64& size, uint8 bus, uint8 device, uint8 function, uint16 offset);
	uint64 GetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset);
	void SetBarVal(uint8 bus, uint8 device, uint8 function, uint16 offset, PciBarKind barKind, uint64 val);
	bool AllocBar(uint8 bus, uint8 device, uint8 function, uint16 offset);
	void AllocRegsForDevice(uint8 bus, uint8 device, uint8 function);

private:
	struct resource_range {
		pci_resource_range def;
		phys_addr_t free;
	};

private:
	DeviceNode* fNode;
	FdtDevice* fFdtDevice {};

	AreaDeleter fConfigArea;
	addr_t fConfigPhysBase {};
	addr_t fConfigBase {};
	size_t fConfigSize {};

	Vector<resource_range> fResourceRanges;
	InterruptMapMask fInterruptMapMask {};
	uint32 fInterruptMapLen {};
	ArrayDeleter<InterruptMap> fInterruptMap;

	AreaDeleter fRegsArea;
	addr_t fRegsPhysBase {};
	PciPldaRegs volatile* fRegs {};
	size_t fRegsSize {};

	MsiInterruptCtrlPlda fIrqCtrl;


	class PciControllerImpl: public BusDriver, public PciController {
	public:
		PciControllerImpl(PciControllerPlda& base): fBase(base) {}

		// BusDriver
		void* QueryInterface(const char* name) final;

		// PciController
		status_t ReadPciConfig(uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 size, uint32* value) final;
		status_t WritePciConfig(uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 size, uint32 value) final;

		status_t GetMaxBusDevices(int32* count) final;

		status_t ReadPciIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8* irq) final;
		status_t WritePciIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8 irq) final;

		status_t GetRange(uint32 index, pci_resource_range* range) final;

		status_t Finalize() final;

		MSIInterface* GetMsiDriver() final;

	public:
		PciControllerPlda& fBase;
	} fPciCtrl;
};
