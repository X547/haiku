/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _ECAM_PCI_H_
#define _ECAM_PCI_H_

#include <bus/PCI.h>

#include <AutoDeleterOS.h>
#include <lock.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define ECAM_PCI_DRIVER_MODULE_NAME "busses/pci/ecam/driver_v1"


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
	size_t size;
	phys_addr_t free;
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


class PciControllerEcam {
public:
	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, PciControllerEcam*& outDriver);
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

private:
	inline status_t InitDriverInt(device_node* node);

	void SetRegisterRange(int kind, phys_addr_t parentBase, phys_addr_t childBase, size_t size);
	inline addr_t ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset);

private:
	struct mutex fLock = MUTEX_INITIALIZER("ECAM PCI");

	device_node* fNode{};

	uint32 fBusCount = 32;

	AreaDeleter fRegsArea;
	uint8 volatile* fRegs{};
	uint64 fRegsLen{};

	RegisterRange fRegisterRanges[3] {};
	InterruptMapMask fInterruptMapMask{};
	uint32 fInterruptMapLen{};
	ArrayDeleter<InterruptMap> fInterruptMap;
};


extern device_manager_info* gDeviceManager;

#endif	// _ECAM_PCI_H_
