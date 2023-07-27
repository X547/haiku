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

	virtual uint8	read_io_8(addr_t mappedIOAddress) = 0;
	virtual void	write_io_8(addr_t mappedIOAddress,uint8 value) = 0;
	virtual uint16	read_io_16(addr_t mappedIOAddress) = 0;
	virtual void	write_io_16(addr_t mappedIOAddress,uint16 value) = 0;
	virtual uint32	read_io_32(addr_t mappedIOAddress) = 0;
	virtual void	write_io_32(addr_t mappedIOAddress,uint32 value) = 0;

	virtual phys_addr_t	ram_address(phys_addr_t physicalAddress) = 0;

	virtual uint32	read_pci_config(uint16 offset, uint8 size) = 0;
	virtual void	write_pci_config(uint16 offset, uint8 size, uint32 value) = 0;
	virtual status_t find_pci_capability(uint8 capID, uint8 *offset) = 0;
	virtual void 	get_pci_info(struct pci_info *info) = 0;
	virtual status_t find_pci_extended_capability(uint16 capID, uint16 *offset) = 0;
	virtual uint8	get_powerstate() = 0;
	virtual void	set_powerstate(uint8 state) = 0;

	// MSI/MSI-X
	virtual uint8	get_msi_count() = 0;
	virtual status_t configure_msi(uint8 count, uint8 *startVector) = 0;
	virtual status_t unconfigure_msi() = 0;

	virtual status_t enable_msi() = 0;
	virtual status_t disable_msi() = 0;

	virtual uint8	get_msix_count() = 0;
	virtual status_t configure_msix(uint8 count, uint8 *startVector) = 0;
	virtual status_t enable_msix() = 0;

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
	// read PCI config space
	virtual status_t read_pci_config(
				uint8 bus, uint8 device, uint8 function,
				uint16 offset, uint8 size, uint32 *value);

	// write PCI config space
	virtual status_t write_pci_config(
				uint8 bus, uint8 device, uint8 function,
				uint16 offset, uint8 size, uint32 value);

	virtual status_t get_max_bus_devices(int32 *count);

	virtual status_t read_pci_irq(
				uint8 bus, uint8 device, uint8 function,
				uint8 pin, uint8 *irq);

	virtual status_t write_pci_irq(
				uint8 bus, uint8 device, uint8 function,
				uint8 pin, uint8 irq);

	virtual status_t get_range(uint32 index, pci_resource_range *range);

	virtual status_t finalize();

protected:
	~PciController() = default;
};

/* Attributes of PCI device nodes */
#define B_PCI_DEVICE_VENDOR_ID			"pci/vendor"				/* uint16 */
#define B_PCI_DEVICE_ID					"pci/id"					/* uint16 */
#define B_PCI_DEVICE_TYPE				"pci/type"
	/* uint16, PCI base class */
#define B_PCI_DEVICE_SUB_TYPE			"pci/subtype"
	/* uint16, PCI sub type */
#define B_PCI_DEVICE_INTERFACE			"pci/interface"
	/* uint16, PCI class API */
#define B_PCI_DEVICE_DOMAIN		"pci/domain"		/* uint32 */
#define B_PCI_DEVICE_BUS		"pci/bus"			/* uint8 */
#define B_PCI_DEVICE_DEVICE		"pci/device"		/* uint8 */
#define B_PCI_DEVICE_FUNCTION	"pci/function"		/* uint8 */

#endif	/* _PCI2_H */
