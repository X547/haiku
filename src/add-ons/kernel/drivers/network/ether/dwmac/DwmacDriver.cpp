#include "DwmacDriver.h"
#include "DwmacNetDevice.h"
#include "kernel_interface.h"

#include <AutoDeleter.h>
#include <util/AutoLock.h>

#include <stdio.h>
#include <string.h>

#include <new>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


DwmacRoster DwmacRoster::sInstance;


float
DwmacDriver::SupportsDevice(device_node* parent)
{
	const char* bus;
	status_t status = gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	const char* compatible;
	status = gDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "snps,dwmac-5.10a") != 0
		&& strcmp(compatible, "starfive,jh7110-eqos-5.20") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
DwmacDriver::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Designware MAC"}},
		{}
	};

	return gDeviceManager->register_node(parent, DWMAC_DRIVER_MODULE_NAME, attrs, NULL, NULL);
}


status_t
DwmacDriver::InitDriver(device_node* node, DwmacDriver*& outDriver)
{
	ObjectDeleter<DwmacDriver> driver(new(std::nothrow) DwmacDriver());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
DwmacDriver::InitDriverInt(device_node* node)
{
	fNode = node;

	RecursiveLocker locker(DwmacRoster::Instance().Lock());

	fId = gDeviceManager->create_id(DWMAC_DEVICE_ID_GENERATOR);
	CHECK_RET(fId);

	DwmacRoster::Instance().Insert(this);

	return B_OK;
}


void
DwmacDriver::UninitDriver()
{
	if (fNetDevice != NULL) {
		fNetDevice->ReleaseDriver();
		fNetDevice = NULL;
	}

	RecursiveLocker locker(DwmacRoster::Instance().Lock());
	DwmacRoster::Instance().Remove(this);

	gDeviceManager->free_id(DWMAC_DEVICE_ID_GENERATOR, fId);
	fId = -1;

	delete this;
}


status_t
DwmacDriver::RegisterChildDevices()
{
	char name[64];
	snprintf(name, sizeof(name), "net/dwmac/%" B_PRId32, fId);

	CHECK_RET(gDeviceManager->publish_device(fNode, name, DWMAC_DEVICE_MODULE_NAME));

	return B_OK;
}
