/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "ECAMPCIController.h"
#include <acpi.h>

#include <AutoDeleterDrivers.h>

#include "acpi_irq_routing_table.h"

#include <string.h>
#include <new>


static uint32
ReadReg8(addr_t adr)
{
	uint32 ofs = adr % 4;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint8 out[4];
	} val{.in = *(vuint32*)adr};
	return val.out[ofs];
}


static uint32
ReadReg16(addr_t adr)
{
	uint32 ofs = adr / 2 % 2;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint16 out[2];
	} val{.in = *(vuint32*)adr};
	return val.out[ofs];
}


static void
WriteReg8(addr_t adr, uint32 value)
{
	uint32 ofs = adr % 4;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint8 out[4];
	} val{.in = *(vuint32*)adr};
	val.out[ofs] = (uint8)value;
	*(vuint32*)adr = val.in;
}


static void
WriteReg16(addr_t adr, uint32 value)
{
	uint32 ofs = adr / 2 % 2;
	adr = adr / 4 * 4;
	union {
		uint32 in;
		uint16 out[2];
	} val{.in = *(vuint32*)adr};
	val.out[ofs] = (uint16)value;
	*(vuint32*)adr = val.in;
}


//#pragma mark - driver


status_t
ECAMPCIController::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	dprintf("+ECAMPCIController::Probe()\n");

	ObjectDeleter<ECAMPCIController> driver;

	AcpiDevice* acpiDevice = node->QueryBusInterface<AcpiDevice>();
	FdtDevice* fdtDevice = node->QueryBusInterface<FdtDevice>();
	if (acpiDevice != NULL) {
		driver.SetTo(new(std::nothrow) ECAMPCIControllerACPI(node, acpiDevice));
	} else if (fdtDevice != NULL) {
		driver.SetTo(new(std::nothrow) ECAMPCIControllerFDT(node, fdtDevice));
	} else {
		return B_ERROR;
	}
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();

	dprintf("-ECAMPCIController::Probe()\n");
	return B_OK;
}


status_t
ECAMPCIController::Init()
{
	CHECK_RET(ReadResourceInfo());

	static const device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "PCI Bus Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = "bus_managers/pci/driver/v1"}},
		{}
	};
	CHECK_RET(fNode->RegisterNode(fNode, static_cast<BusDriver*>(&fBusManager), attrs, NULL));

	return B_OK;
}


void* ECAMPCIController::BusManager::QueryInterface(const char* name)
{
	if (strcmp(name, PciController::ifaceName) == 0)
		return static_cast<PciController*>(&fBase);

	return NULL;
}


addr_t
ECAMPCIController::ConfigAddress(uint8 bus, uint8 device, uint8 function, uint16 offset)
{
	PciAddressEcam address {
		.offset = offset,
		.function = function,
		.device = device,
		.bus = bus
	};
	if ((address.val + 4) > fRegsLen)
		return 0;

	return (addr_t)fRegs + address.val;
}


//#pragma mark - PCI controller


status_t
ECAMPCIController::ReadPciConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32* value)
{
	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: *value = ReadReg8(address); break;
		case 2: *value = ReadReg16(address); break;
		case 4: *value = *(vuint32*)address; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


status_t
ECAMPCIController::WritePciConfig(uint8 bus, uint8 device, uint8 function,
	uint16 offset, uint8 size, uint32 value)
{
	addr_t address = ConfigAddress(bus, device, function, offset);
	if (address == 0)
		return B_ERROR;

	switch (size) {
		case 1: WriteReg8(address, value); break;
		case 2: WriteReg16(address, value); break;
		case 4: *(vuint32*)address = value; break;
		default:
			return B_ERROR;
	}

	return B_OK;
}


status_t
ECAMPCIController::GetMaxBusDevices(int32* count)
{
	*count = 32;
	return B_OK;
}


status_t
ECAMPCIController::ReadPciIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8* irq)
{
	return B_UNSUPPORTED;
}


status_t
ECAMPCIController::WritePciIrq(uint8 bus, uint8 device, uint8 function,
	uint8 pin, uint8 irq)
{
	return B_UNSUPPORTED;
}


status_t
ECAMPCIController::GetRange(uint32 index, pci_resource_range* range)
{
	if (index >= (uint32)fResourceRanges.Count())
		return B_BAD_INDEX;

	*range = fResourceRanges[index];
	return B_OK;
}


MSIInterface*
ECAMPCIController::GetMsiDriver()
{
	if (!msi_supported())
		return NULL;

	return &fMsiIface;
}


status_t
ECAMPCIController::MSIInterfaceImpl::AllocateVectors(uint32 count, uint32& startVector,
	uint64& address, uint32& data)
{
	return msi_allocate_vectors(count, &startVector, &address, &data);
}


void
ECAMPCIController::MSIInterfaceImpl::FreeVectors(uint32 count, uint32 startVector)
{
	msi_free_vectors(count, startVector);
}
