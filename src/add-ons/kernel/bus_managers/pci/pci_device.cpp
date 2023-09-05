/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2002-2003, Thomas Kurschel. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <AutoDeleter.h>
#include <util/Vector.h>

#include "pci.h"
#include "pci_private.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define PCI_BUS_DRIVER_MODULE_NAME "bus_managers/pci/driver/v1"


class PciBusImpl: public DeviceDriver {
public:
	PciBusImpl(DeviceNode* node): fNode(node) {}
	virtual ~PciBusImpl() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	DeviceNode* fNode;
};


class PciDeviceImpl: public PciDevice, public BusDriver {
public:
				PciDeviceImpl(PciBusImpl* driver, PCIDev* device):
					fDriver(driver), fDevice(device) {}
	virtual		~PciDeviceImpl() = default;

	// BusDriver
	void		Free() final {delete this;}
	status_t	InitDriver(DeviceNode* node) final;
	void*		QueryInterface(const char* name) final;

	// PciDevice
	uint8		ReadIo8(addr_t mappedIOAddress) final;
	void		WriteIo8(addr_t mappedIOAddress, uint8 value) final;
	uint16		ReadIo16(addr_t mappedIOAddress) final;
	void		WriteIo16(addr_t mappedIOAddress, uint16 value) final;
	uint32		ReadIo32(addr_t mappedIOAddress) final;
	void		WriteIo32(addr_t mappedIOAddress, uint32 value) final;

	phys_addr_t RamAddress(phys_addr_t physicalAddress) final;

	uint32		ReadPciConfig(uint16 offset, uint8 size) final;
	void		WritePciConfig(uint16 offset, uint8 size, uint32 value) final;
	status_t	FindPciCapability(uint8 capID, uint8* offset) final;
	void		GetPciInfo(struct pci_info* info) final;
	status_t	FindPciExtendedCapability(uint16 capID, uint16* offset) final;
	uint8 		GetPowerstate() final;
	void 		SetPowerstate(uint8 state) final;

	// MSI/MSI-X
	uint8		GetMsiCount() final;
	status_t	ConfigureMsi(uint8 count, uint8* startVector) final;
	status_t	UnconfigureMsi() final;

	status_t	EnableMsi() final;
	status_t	DisableMsi() final;

	uint8		GetMsixCount() final;
	status_t	ConfigureMsix(uint8 count, uint8* startVector) final;
	status_t	EnableMsix() final;

private:
	PciBusImpl* fDriver;
	PCIDev* fDevice;
	DeviceNode* fNode {};
};


// #pragma mark - PciBusImpl

status_t
PciBusImpl::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<PciBusImpl> driver(new(std::nothrow) PciBusImpl(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
PciBusImpl::Init()
{
	PciController* ctrl = fNode->QueryBusInterface<PciController>();

	int32 ctrlDomain = gPCI->AddController(ctrl, fNode);
	CHECK_RET(ctrlDomain);

	pci_info info;
	for (int32 i = 0; gPCI->GetNthInfo(i, &info) == B_OK; i++) {
		uint8 domain;
		uint8 bus;
		if (gPCI->ResolveVirtualBus(info.bus, &domain, &bus) != B_OK) {
			dprintf("ResolveVirtualBus(%u) failed\n", info.bus);
			continue;
		}

		if (ctrlDomain != domain)
			continue;

		PCIDev* dev = gPCI->FindDevice(domain, bus, info.device, info.function);

		ObjectDeleter<PciDeviceImpl> pciDev(new(std::nothrow) PciDeviceImpl(this, dev));
		if (!pciDev.IsSet()) {
			return B_NO_MEMORY;
		}

		device_attr attrs[] = {
			{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "PCI Device"}},
			{B_DEVICE_BUS,         B_STRING_TYPE, {.string = "pci"}},

			{B_PCI_DEVICE_VENDOR_ID, B_UINT16_TYPE, {.ui16 = info.vendor_id}},
			{B_PCI_DEVICE_ID,        B_UINT16_TYPE, {.ui16 = info.device_id}},
			{B_PCI_DEVICE_TYPE,      B_UINT16_TYPE, {.ui16 = info.class_base}},
			{B_PCI_DEVICE_SUB_TYPE,  B_UINT16_TYPE, {.ui16 = info.class_sub}},
			{B_PCI_DEVICE_INTERFACE, B_UINT16_TYPE, {.ui16 = info.class_api}},

			{B_PCI_DEVICE_DOMAIN,   B_UINT32_TYPE, {.ui32 = domain}},
			{B_PCI_DEVICE_BUS,      B_UINT8_TYPE,  {.ui8  = bus}},
			{B_PCI_DEVICE_DEVICE,   B_UINT8_TYPE,  {.ui8  = info.device}},
			{B_PCI_DEVICE_FUNCTION, B_UINT8_TYPE,  {.ui8  = info.function}},

			{}
		};

		CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(pciDev.Detach()), &attrs[0], NULL));
	}

	return B_OK;
}


// #pragma mark - PciDeviceImpl

status_t
PciDeviceImpl::InitDriver(DeviceNode* node)
{
	fNode = node;
	return B_OK;
}


void*
PciDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, PciDevice::ifaceName) == 0)
		return static_cast<PciDevice*>(this);

	return NULL;
}


uint8
PciDeviceImpl::ReadIo8(addr_t mappedIOAddress)
{
	return pci_read_io_8(mappedIOAddress);
}


void
PciDeviceImpl::WriteIo8(addr_t mappedIOAddress, uint8 value)
{
	pci_write_io_8(mappedIOAddress, value);
}


uint16
PciDeviceImpl::ReadIo16(addr_t mappedIOAddress)
{
	return pci_read_io_16(mappedIOAddress);
}


void
PciDeviceImpl::WriteIo16(addr_t mappedIOAddress, uint16 value)
{
	pci_write_io_16(mappedIOAddress, value);
}


uint32
PciDeviceImpl::ReadIo32(addr_t mappedIOAddress)
{
	return pci_read_io_32(mappedIOAddress);
}


void
PciDeviceImpl::WriteIo32(addr_t mappedIOAddress, uint32 value)
{
	pci_write_io_32(mappedIOAddress, value);
}


phys_addr_t
PciDeviceImpl::RamAddress(phys_addr_t physicalAddress)
{
	return pci_ram_address(physicalAddress);
}


uint32
PciDeviceImpl::ReadPciConfig(uint16 offset, uint8 size)
{
	return gPCI->ReadConfig(fDevice, offset, size);
}


void
PciDeviceImpl::WritePciConfig(uint16 offset, uint8 size, uint32 value)
{
	gPCI->WriteConfig(fDevice, offset, size, value);
}


status_t
PciDeviceImpl::FindPciCapability(uint8 capID, uint8* offset)
{
	return gPCI->FindCapability(fDevice, capID, offset);
}


void
PciDeviceImpl::GetPciInfo(struct pci_info* info)
{
	if (info == NULL)
		return;
	*info = fDevice->info;
}


status_t
PciDeviceImpl::FindPciExtendedCapability(uint16 capID, uint16* offset)
{
	return gPCI->FindExtendedCapability(fDevice, capID, offset);
}


uint8
PciDeviceImpl::GetPowerstate()
{
	return gPCI->GetPowerstate(fDevice);
}


void
PciDeviceImpl::SetPowerstate(uint8 state)
{
	gPCI->SetPowerstate(fDevice, state);
}


uint8
PciDeviceImpl::GetMsiCount()
{
	return gPCI->GetMSICount(fDevice);
}


status_t
PciDeviceImpl::ConfigureMsi(uint8 count, uint8* startVector)
{
	return gPCI->ConfigureMSI(fDevice, count, startVector);
}


status_t
PciDeviceImpl::UnconfigureMsi()
{
	return gPCI->UnconfigureMSI(fDevice);
}


status_t
PciDeviceImpl::EnableMsi()
{
	return gPCI->EnableMSI(fDevice);
}


status_t
PciDeviceImpl::DisableMsi()
{
	return gPCI->DisableMSI(fDevice);
}


uint8
PciDeviceImpl::GetMsixCount()
{
	return gPCI->GetMSIXCount(fDevice);
}


status_t
PciDeviceImpl::ConfigureMsix(uint8 count, uint8* startVector)
{
	return gPCI->ConfigureMSIX(fDevice, count, startVector);
}


status_t
PciDeviceImpl::EnableMsix()
{
	return gPCI->EnableMSIX(fDevice);
}


driver_module_info gPciBusDriverModule = {
	.info = {
		.name = PCI_BUS_DRIVER_MODULE_NAME,
	},
	.probe = PciBusImpl::Probe
};
