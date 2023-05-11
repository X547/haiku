#pragma once

#include <net_device.h>


class DwmacDriver;


class DwmacNetDevice: public net_device {
public:
	static status_t InitDevice(const char* name, DwmacNetDevice*& outDevice);
	status_t UninitDevice();

	status_t Up();
	void Down();

	status_t Control(int32 op, void* argument, size_t length);

	status_t SendData(net_buffer* buffer);
	status_t ReceiveData(net_buffer*& _buffer);

	status_t SetMtu(size_t mtu);
	status_t SetPromiscuous(bool promiscuous);
	status_t SetMedia(uint32 media);

	status_t AddMulticast(const struct sockaddr* address);
	status_t RemoveMulticast(const struct sockaddr* address);

	void ReleaseDriver() {fDriver = NULL;}

private:
	DwmacDriver* fDriver {};

	inline status_t InitDeviceInt(DwmacDriver* driver);
};
