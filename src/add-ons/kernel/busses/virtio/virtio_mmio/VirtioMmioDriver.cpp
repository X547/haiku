/*
 * Copyright 2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioMmioDevice.h"

#include <string.h>
#include <new>

#include <dm2/bus/FDT.h>
#include <dm2/bus/ACPI.h>

#include <AutoDeleterDM2.h>
#include <acpi.h>


#define VIRTIO_MMIO_DRIVER_MODULE_NAME "busses/virtio/virtio_mmio/driver/v1"


struct virtio_memory_range {
	uint64 base;
	uint64 length;
};


static acpi_status
virtio_crs_find_address(acpi_resource *res, void *context)
{
	virtio_memory_range &range = *((virtio_memory_range *)context);

	if (res->type == ACPI_RESOURCE_TYPE_FIXED_MEMORY32) {
		range.base = res->data.fixed_memory32.address;
		range.length = res->data.fixed_memory32.address_length;
	}

	return B_OK;
}


status_t
VirtioMmioDeviceDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<VirtioMmioDeviceDriver> driver(new(std::nothrow) VirtioMmioDeviceDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
VirtioMmioDeviceDriver::Init()
{
	TRACE("init_device(%p)\n", fNode);

	uint64 regs = 0;
	uint64 regsLen = 0;
	uint64 interrupt = 0;

	FdtDevice* fdtDev = fNode->QueryBusInterface<FdtDevice>();
	AcpiDevice* acpiDev = fNode->QueryBusInterface<AcpiDevice>();
	if (fdtDev != NULL) {
		// initialize virtio device from FDT
		for (uint32 i = 0; fdtDev->GetReg(i, &regs, &regsLen); i++) {
			TRACE("  reg[%" B_PRIu32 "]: (0x%" B_PRIx64 ", 0x%" B_PRIx64 ")\n",
				i, regs, regsLen);
		}

		if (!fdtDev->GetReg(0, &regs, &regsLen)) {
			ERROR("  no regs\n");
			return B_ERROR;
		}

		if (!fdtDev->GetInterrupt(0, NULL, &interrupt)) {
			ERROR("  no interrupts\n");
			return B_ERROR;
		}
	} else if (acpiDev != NULL) {
		// initialize virtio device from ACPI
		virtio_memory_range range = { 0, 0 };
		acpiDev->WalkResources((char *)"_CRS", virtio_crs_find_address, &range);
		regs = range.base;
		regsLen = range.length;
	} else {
		return B_ERROR;
	}

	ObjectDeleter<VirtioMmioBusDriver> busDriver(new(std::nothrow) VirtioMmioBusDriver(fDevice));
	if (!busDriver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fDevice.Init(regs, regsLen, interrupt, 1));

	DeviceNode* childNode {};
	CHECK_RET(fNode->RegisterNode(busDriver.Detach(), &childNode));

	return B_OK;
}


void
VirtioMmioDeviceDriver::Free()
{
	delete this;
}


void
VirtioMmioBusDriver::Free()
{
	delete this;
}


status_t
VirtioMmioBusDriver::InitDriver(DeviceNode* node)
{
	fAttrs.Add({ B_DEVICE_PRETTY_NAME,    B_STRING_TYPE, {.string = "Virtio MMIO"} });
	fAttrs.Add({ B_DEVICE_BUS,            B_STRING_TYPE, {.string = "virtio"} });
	fAttrs.Add({ "virtio/version",        B_UINT32_TYPE, {.ui32 = fDevice.fRegs->version} });
	fAttrs.Add({ "virtio/device_id",      B_UINT32_TYPE, {.ui32 = fDevice.fRegs->deviceId} });
	fAttrs.Add({ VIRTIO_DEVICE_TYPE_ITEM, B_UINT16_TYPE, {.ui16 = (uint16)fDevice.fRegs->deviceId} });
	fAttrs.Add({ "virtio/vendor_id",      B_UINT32_TYPE, {.ui32 = fDevice.fRegs->vendorId} });
	fAttrs.Add({});

	return B_OK;
}


const device_attr*
VirtioMmioBusDriver::Attributes() const
{
	return &fAttrs[0];
}


void*
VirtioMmioBusDriver::QueryInterface(const char* name)
{
	if (strcmp(name, VirtioDevice::ifaceName) == 0)
		return static_cast<VirtioDevice*>(&fDevice);

	return NULL;
}


static driver_module_info sVirtioMmioDriver = {
	.info = {
		.name = VIRTIO_MMIO_DRIVER_MODULE_NAME,
	},
	.probe = VirtioMmioDeviceDriver::Probe
};


module_info* modules[] = {
	(module_info* )&sVirtioMmioDriver,
	NULL
};
