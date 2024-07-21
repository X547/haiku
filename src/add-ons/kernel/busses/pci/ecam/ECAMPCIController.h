/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _ECAM_PCI_CONTROLLER_H_
#define _ECAM_PCI_CONTROLLER_H_

#include <dm2/bus/PCI.h>
#include <dm2/bus/FDT.h>
#include <dm2/bus/ACPI.h>

#include <AutoDeleterOS.h>
#include <lock.h>
#include <util/Vector.h>

#include <arch/generic/msi.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define ECAM_PCI_DRIVER_MODULE_NAME "busses/pci/ecam/driver/v1"


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


enum PciBarKind {
	kRegIo,
	kRegMmio32,
	kRegMmio64,
	kRegMmio1MB,
	kRegUnknown,
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


class ECAMPCIController: public DeviceDriver, public PciController {
public:
	ECAMPCIController(DeviceNode* node): fNode(node), fBusManager(*this) {}
	virtual ~ECAMPCIController() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** outDriver);
	void Free() final {delete this;}

	// PciController
	status_t ReadPciConfig(
				uint8 bus, uint8 device, uint8 function,
				uint16 offset, uint8 size, uint32* value) final;

	status_t WritePciConfig(
				uint8 bus, uint8 device, uint8 function,
				uint16 offset, uint8 size, uint32 value) final;

	status_t GetMaxBusDevices(int32* count) final;

	status_t ReadPciIrq(
				uint8 bus, uint8 device, uint8 function,
				uint8 pin, uint8* irq) final;

	status_t WritePciIrq(
				uint8 bus, uint8 device, uint8 function,
				uint8 pin, uint8 irq) final;

	status_t GetRange(uint32 index, pci_resource_range* range) final;

	MSIInterface* GetMsiDriver() final;

private:
	status_t Init();
	inline addr_t ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset);

protected:
	virtual status_t ReadResourceInfo() = 0;

protected:
	struct mutex fLock = MUTEX_INITIALIZER("ECAM PCI");

	DeviceNode* fNode{};

	AreaDeleter fRegsArea;
	uint8 volatile* fRegs{};
	uint64 fRegsLen{};

	Vector<pci_resource_range> fResourceRanges;

	class BusManager: public BusDriver {
	public:
		BusManager(ECAMPCIController& base): fBase(base) {}

		void* QueryInterface(const char* name) final;

	private:
		ECAMPCIController& fBase;
	} fBusManager;

	class MSIInterfaceImpl: public MSIInterface {
	public:
		status_t AllocateVectors(uint32 count, uint32& startVector, uint64& address, uint32& data) final;
		void FreeVectors(uint32 count, uint32 startVector) final;
	} fMsiIface;
};


class ECAMPCIControllerACPI: public ECAMPCIController {
public:
	ECAMPCIControllerACPI(DeviceNode* node, AcpiDevice* acpiDevice): ECAMPCIController(node), fAcpiDevice(acpiDevice) {}
	~ECAMPCIControllerACPI() = default;

	status_t Finalize() final;

protected:
	status_t ReadResourceInfo() final;
	// status_t ReadResourceInfo(device_node* parent);

	AcpiDevice* fAcpiDevice;

	uint8 fStartBusNumber{};
	uint8 fEndBusNumber{};

private:
	friend class X86PCIControllerMethPcie;

	static acpi_status AcpiCrsScanCallback(acpi_resource *res, void *context);
	inline acpi_status AcpiCrsScanCallbackInt(acpi_resource *res);
};


class ECAMPCIControllerFDT: public ECAMPCIController {
public:
	ECAMPCIControllerFDT(DeviceNode* node, FdtDevice* fdtDevice): ECAMPCIController(node), fFdtDevice(fdtDevice) {}
	~ECAMPCIControllerFDT() = default;

	status_t Finalize() final;

protected:
	status_t ReadResourceInfo() final;

	FdtDevice* fFdtDevice;

private:
	static void FinalizeInterrupts(FdtInterruptMap* interruptMap, int bus, int device, int function);
};


extern pci_module_info* gPCI;

#endif	// _ECAM_PCI_CONTROLLER_H_
