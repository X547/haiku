#include "DwmacNetDevice.h"
#include "DwmacDriver.h"

#include <AutoDeleter.h>
#include <util/AutoLock.h>

#include <stdio.h>
#include <string.h>

#include <new>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


static status_t
StringToInt32(const char* str, int32& value)
{
	value = atoi(str);
	char checkStr[32];
	sprintf(checkStr, "%" B_PRId32, value);
	if (strcmp(str, checkStr) != 0)
		return B_BAD_VALUE;

	return B_OK;
}


status_t
DwmacNetDevice::InitDevice(const char* name, DwmacNetDevice*& outDevice)
{
	const char prefix[] = "/dev/net/dwmac/";
	size_t prefixLen = strlen(prefix);
	if (strncmp(prefix, name, prefixLen) != 0)
		return B_BAD_VALUE;

	RecursiveLocker locker(DwmacRoster::Instance().Lock());

	int32 id;
	CHECK_RET(StringToInt32(name + prefixLen, id));
	DwmacDriver* driver = DwmacRoster::Instance().Lookup(id);
	if (driver == NULL)
		return B_BAD_VALUE;

	if (driver->NetDevice() != NULL)
		return EEXIST;

	ObjectDeleter<DwmacNetDevice> device(new(std::nothrow) DwmacNetDevice());
	CHECK_RET(device->InitDeviceInt(driver));
	driver->SetNetDevice(device.Get());
	outDevice = device.Detach();
	return B_OK;
}


status_t
DwmacNetDevice::InitDeviceInt(DwmacDriver* driver)
{
	fDriver = driver;
	return B_OK;
}


status_t
DwmacNetDevice::UninitDevice()
{
	delete this;
	return B_OK;
}


status_t
DwmacNetDevice::Up()
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


void
DwmacNetDevice::Down()
{
	if (fDriver == NULL)
		return;
}


status_t
DwmacNetDevice::Control(int32 op, void* argument, size_t length)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_DEV_INVALID_IOCTL;
}


status_t
DwmacNetDevice::SendData(net_buffer* buffer)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


status_t
DwmacNetDevice::ReceiveData(net_buffer*& _buffer)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


status_t
DwmacNetDevice::SetMtu(size_t mtu)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


status_t
DwmacNetDevice::SetPromiscuous(bool promiscuous)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


status_t
DwmacNetDevice::SetMedia(uint32 media)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


status_t
DwmacNetDevice::AddMulticast(const struct sockaddr* address)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}


status_t
DwmacNetDevice::RemoveMulticast(const struct sockaddr* address)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_ERROR;
}
