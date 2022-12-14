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


class PciControllerX86 {
public:
	virtual ~PciControllerX86() = default;

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, PciControllerX86*& outDriver);
	void UninitDriver();

	virtual status_t ReadConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 &value) = 0;

	virtual status_t WriteConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) = 0;

	virtual status_t GetMaxBusDevices(int32& count) = 0;

	status_t ReadIrq(
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8& irq);

	status_t WriteIrq(
		uint8 bus, uint8 device, uint8 function,
		uint8 pin, uint8 irq);

protected:
	static status_t CreateDriver(device_node* node, PciControllerX86* driver, PciControllerX86*& driverOut);
	virtual status_t InitDriverInt(device_node* node);

protected:
	spinlock fLock = B_SPINLOCK_INITIALIZER;

	device_node* fNode{};

	addr_t fPCIeBase{};
	uint8 fStartBusNumber{};
	uint8 fEndBusNumber{};
};


class PciControllerX86Meth1: public PciControllerX86 {
public:
	virtual ~PciControllerX86Meth1() = default;

	status_t InitDriverInt(device_node* node) override;

	status_t ReadConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 &value) override;

	status_t WriteConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) override;

	status_t GetMaxBusDevices(int32& count) override;
};

class PciControllerX86Meth2: public PciControllerX86 {
public:
	virtual ~PciControllerX86Meth2() = default;

	status_t InitDriverInt(device_node* node) final;

	status_t ReadConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 &value) final;

	status_t WriteConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) final;

	status_t GetMaxBusDevices(int32& count) final;
};

class PciControllerX86MethPcie: public PciControllerX86Meth1 {
public:
	virtual ~PciControllerX86MethPcie() = default;

	status_t InitDriverInt(device_node* node) final;

	status_t ReadConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 &value) final;

	status_t WriteConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) final;

	status_t GetMaxBusDevices(int32& count) final;
};

class PciControllerX86MethBios: public PciControllerX86 {
public:
	virtual ~PciControllerX86MethBios() = default;

	status_t InitDriverInt(device_node* node) final;

	status_t ReadConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 &value) final;

	status_t WriteConfig(
		uint8 bus, uint8 device, uint8 function,
		uint16 offset, uint8 size, uint32 value) final;

	status_t GetMaxBusDevices(int32& count) final;
};


extern device_manager_info* gDeviceManager;

#endif	// _ECAM_PCI_H_
