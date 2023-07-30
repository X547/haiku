/*
 * Copyright 2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioMmioDevice.h"

#include <string.h>
#include <new>

#include <dm2/bus/FDT.h>

#include <AutoDeleterDM2.h>


#define VIRTIO_MMIO_DRIVER_MODULE_NAME "busses/virtio/virtio_mmio/driver/v1"


status_t VirtioMmioDeviceDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
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
	} else {
		return B_ERROR;
	}

	// TODO: initialize virtio device from ACPI

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
