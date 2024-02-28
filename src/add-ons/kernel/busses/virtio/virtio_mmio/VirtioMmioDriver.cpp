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


#define VIRTIO_MMIO_FDT_DRIVER_MODULE_NAME "busses/virtio/virtio_mmio/fdt/driver/v1"
#define VIRTIO_MMIO_ACPI_DRIVER_MODULE_NAME "busses/virtio/virtio_mmio/acpi/driver/v1"


status_t
VirtioMmioDeviceDriver::ProbeFdt(DeviceNode* node, DeviceDriver** outDriver)
{
	TRACE("VirtioMmioDeviceDriver::ProbeFdt(%p)\n", node);

	uint64 regs = 0;
	uint64 regsLen = 0;
	uint64 interrupt = 0;

	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
	for (uint32 i = 0; fdtDev->GetReg(i, &regs, &regsLen); i++) {
		TRACE("  reg[%" B_PRIu32 "]: (0x%" B_PRIx64 ", 0x%" B_PRIx64 ")\n",
			i, regs, regsLen);
	}

	if (!fdtDev->GetReg(0, &regs, &regsLen)) {
		ERROR("  no regs\n");
		return ENODEV;
	}

	if (!fdtDev->GetInterrupt(0, NULL, &interrupt)) {
		ERROR("  no interrupts\n");
		return ENODEV;
	}

	ObjectDeleter<VirtioMmioDeviceDriver> driver(new(std::nothrow) VirtioMmioDeviceDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init(regs, regsLen, interrupt));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
VirtioMmioDeviceDriver::ProbeAcpi(DeviceNode* node, DeviceDriver** outDriver)
{
	TRACE("VirtioMmioDeviceDriver::ProbeAcpi(%p)\n", node);

	uint64 regs = 0;
	uint64 regsLen = 0;
	uint64 interrupt = 0;

	AcpiDevice* acpiDev = node->QueryBusInterface<AcpiDevice>();

	auto findAddress = [&regs, &regsLen, &interrupt](acpi_resource *res) {
		switch (res->type) {
			case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
				regs = res->data.fixed_memory32.address;
				regsLen = res->data.fixed_memory32.address_length;
				break;
			case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
				interrupt = res->data.extended_irq.interrupts[0];
				break;
		}
		return B_OK;
	};

	acpiDev->WalkResources((char *)"_CRS", findAddress);
	if (regsLen == 0)
		return ENODEV;

	ObjectDeleter<VirtioMmioDeviceDriver> driver(new(std::nothrow) VirtioMmioDeviceDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init(regs, regsLen, interrupt));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
VirtioMmioDeviceDriver::Init(uint64 regs, uint64 regsLen, uint64 interrupt)
{
	dprintf("VirtioMmioDeviceDriver::Init(%#" B_PRIx64 ", %#" B_PRIx64 ", %" B_PRIu64 ")\n", regs, regsLen, interrupt);

	CHECK_RET(fDevice.Init(regs, regsLen, interrupt, 1));

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME,    B_STRING_TYPE, {.string = "Virtio MMIO"}},
		{B_DEVICE_BUS,            B_STRING_TYPE, {.string = "virtio"}},
		{"virtio/version",        B_UINT32_TYPE, {.ui32 = fDevice.fRegs->version}},
		{"virtio/device_id",      B_UINT32_TYPE, {.ui32 = fDevice.fRegs->deviceId}},
		{VIRTIO_DEVICE_TYPE_ITEM, B_UINT16_TYPE, {.ui16 = (uint16)fDevice.fRegs->deviceId}},
		{"virtio/vendor_id",      B_UINT32_TYPE, {.ui32 = fDevice.fRegs->vendorId}},
		{}
	};

	CHECK_RET(fNode->RegisterNode(fNode, &fBusDriver, attrs, NULL));

	return B_OK;
}


void
VirtioMmioDeviceDriver::Free()
{
	delete this;
}


void*
VirtioMmioBusDriver::QueryInterface(const char* name)
{
	if (strcmp(name, VirtioDevice::ifaceName) == 0)
		return static_cast<VirtioDevice*>(&fDevice);

	return NULL;
}


static driver_module_info sVirtioMmioFdtDriver = {
	.info = {
		.name = VIRTIO_MMIO_FDT_DRIVER_MODULE_NAME,
	},
	.probe = VirtioMmioDeviceDriver::ProbeFdt
};

static driver_module_info sVirtioMmioAcpiDriver = {
	.info = {
		.name = VIRTIO_MMIO_ACPI_DRIVER_MODULE_NAME,
	},
	.probe = VirtioMmioDeviceDriver::ProbeAcpi
};


module_info* modules[] = {
	(module_info* )&sVirtioMmioFdtDriver,
	(module_info* )&sVirtioMmioAcpiDriver,
	NULL
};
