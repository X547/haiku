#include "DwmacNetDevice.h"
#include "DwmacDriver.h"

#include <ethernet.h>

#include <AutoDeleter.h>
#include <util/AutoLock.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

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
DwmacNetDevice::InitDevice(const char* name, net_device*& outDevice)
{
	dprintf("DwmacNetDevice::InitDevice(\"%s\")\n", name);

	const char prefix[] = "/dev/net/dwmac/";
	size_t prefixLen = strlen(prefix);
	if (strncmp(prefix, name, prefixLen) != 0)
		return B_BAD_VALUE;

	dprintf("  (1)\n");
	RecursiveLocker locker(DwmacRoster::Instance().Lock());

	int32 id;
	CHECK_RET(StringToInt32(name + prefixLen, id));
	dprintf("  id: %" B_PRId32 "\n", id);
	DwmacDriver* driver = DwmacRoster::Instance().Lookup(id);
	dprintf("  driver: %p\n", driver);
	if (driver == NULL)
		return B_BAD_VALUE;
#if 0
	if (driver->NetDevice() != NULL)
		return EEXIST;
#endif
	dprintf("  (2)\n");
	ObjectDeleter<DwmacNetDevice> device(new(std::nothrow) DwmacNetDevice());
	strcpy(device->fNetDev.name, name);
	CHECK_RET(device->InitDeviceInt(driver));
#if 0
	driver->SetNetDevice(device.Get());
#endif
	outDevice = device.Detach()->ToNetDevice();
	dprintf("  (3)\n");
	return B_OK;
}


status_t
DwmacNetDevice::InitDeviceInt(DwmacDriver* driver)
{
	fDriver = driver;

	fNetDev.flags = IFF_BROADCAST | IFF_LINK;
	fNetDev.type = IFT_ETHER;
	fNetDev.mtu = 1500;
	fNetDev.media = IFM_ACTIVE | IFM_ETHER;
	fNetDev.header_length = ETHER_HEADER_LENGTH;

	fFrameSize = ETHER_MAX_FRAME_SIZE;

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

	return B_OK;
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

	dprintf("DwmacNetDevice::Control(%#" B_PRIx32 ")\n", op);

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
DwmacNetDevice::SetMtu(size_t mtu)
{
	if (fDriver == NULL)
		return B_ERROR;

	if (mtu > fFrameSize - ETHER_HEADER_LENGTH || mtu <= ETHER_HEADER_LENGTH + 10)
		return B_BAD_VALUE;

	fNetDev.mtu = mtu;
	return B_OK;
}


status_t
DwmacNetDevice::SetPromiscuous(bool promiscuous)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_NOT_SUPPORTED;
}


status_t
DwmacNetDevice::SetMedia(uint32 media)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_NOT_SUPPORTED;
}


status_t
DwmacNetDevice::AddMulticast(const struct sockaddr* address)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_NOT_SUPPORTED;
}


status_t
DwmacNetDevice::RemoveMulticast(const struct sockaddr* address)
{
	if (fDriver == NULL)
		return B_ERROR;

	return B_NOT_SUPPORTED;
}
