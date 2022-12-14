/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _PCICONTROLLERX86_H_
#define _PCICONTROLLERX86_H_

#include <bus/PCI.h>

#include <AutoDeleterOS.h>
#include <lock.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define PCI_X86_DRIVER_MODULE_NAME "busses/pci/x86/driver_v1"


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

	status_t GetRange(uint32 index, pci_resource_range* range);

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


extern device_manager_info* gDeviceManager;

#endif	// _PCICONTROLLERX86_H_
