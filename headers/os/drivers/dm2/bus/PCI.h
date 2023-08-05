/*
 * Copyright 2008, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PCI2_H
#define _PCI2_H


#include <dm2/device_manager.h>
#include <PCI.h>


class PciDevice {
public:
	static inline const char ifaceName[] = "bus_managers/pci/device";

	virtual uint8		ReadIo8(addr_t mappedIOAddress) = 0;
	virtual void		WriteIo8(addr_t mappedIOAddress, uint8 value) = 0;
	virtual uint16		ReadIo16(addr_t mappedIOAddress) = 0;
	virtual void		WriteIo16(addr_t mappedIOAddress, uint16 value) = 0;
	virtual uint32		ReadIo32(addr_t mappedIOAddress) = 0;
	virtual void		WriteIo32(addr_t mappedIOAddress, uint32 value) = 0;

	virtual phys_addr_t RamAddress(phys_addr_t physicalAddress) = 0;

	virtual uint32		ReadPciConfig(uint16 offset, uint8 size) = 0;
	virtual void		WritePciConfig(uint16 offset, uint8 size, uint32 value) = 0;
	virtual status_t	FindPciCapability(uint8 capID, uint8* offset) = 0;
	virtual void		GetPciInfo(struct pci_info* info) = 0;
	virtual status_t	FindPciExtendedCapability(uint16 capID, uint16* offset) = 0;
	virtual uint8 		GetPowerstate() = 0;
	virtual void 		SetPowerstate(uint8 state) = 0;

	// MSI/MSI-X
	virtual uint8		GetMsiCount() = 0;
	virtual status_t	ConfigureMsi(uint8 count, uint8* startVector) = 0;
	virtual status_t	UnconfigureMsi() = 0;

	virtual status_t	EnableMsi() = 0;
	virtual status_t	DisableMsi() = 0;

	virtual uint8		GetMsixCount() = 0;
	virtual status_t	ConfigureMsix(uint8 count, uint8* startVector) = 0;
	virtual status_t	EnableMsix() = 0;

protected:
	~PciDevice() = default;
};


enum {
	kPciRangeInvalid      = 0,
	kPciRangeIoPort       = 1,
	kPciRangeMmio         = 2,
	kPciRangeMmio64Bit    = 1 << 0,
	kPciRangeMmioPrefetch = 1 << 1,
	kPciRangeMmioEnd      = 6,
	kPciRangeEnd          = 6,
};

typedef struct pci_resource_range {
	uint32 type;
	phys_addr_t host_addr;
	phys_addr_t pci_addr;
	uint64 size;
} pci_resource_range;


class PciController {
public:
	static inline const char ifaceName[] = "busses/pci/device";

	virtual status_t ReadPciConfig(uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 size, uint32* value) = 0;
	virtual status_t WritePciConfig(uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 size, uint32 value) = 0;

	virtual status_t GetMaxBusDevices(int32* count) = 0;

	virtual status_t ReadPciIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8* irq) = 0;
	virtual status_t WritePciIrq(uint8 bus, uint8 device, uint8 function, uint8 pin, uint8 irq) = 0;

	virtual status_t GetRange(uint32 index, pci_resource_range* range) = 0;

	virtual status_t Finalize() = 0;

protected:
	~PciController() = default;
};

/* Attributes of PCI device nodes */
#define B_PCI_DEVICE_VENDOR_ID	"pci/vendor"		/* uint16 */
#define B_PCI_DEVICE_ID			"pci/id"			/* uint16 */
#define B_PCI_DEVICE_TYPE		"pci/type"			/* uint16, PCI base class */
#define B_PCI_DEVICE_SUB_TYPE	"pci/subtype"		/* uint16, PCI sub type */
#define B_PCI_DEVICE_INTERFACE	"pci/interface"		/* uint16, PCI class API */

#define B_PCI_DEVICE_DOMAIN		"pci/domain"		/* uint32 */
#define B_PCI_DEVICE_BUS		"pci/bus"			/* uint8 */
#define B_PCI_DEVICE_DEVICE		"pci/device"		/* uint8 */
#define B_PCI_DEVICE_FUNCTION	"pci/function"		/* uint8 */

#endif	/* _PCI2_H */
