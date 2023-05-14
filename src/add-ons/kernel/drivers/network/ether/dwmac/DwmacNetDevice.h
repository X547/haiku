#pragma once

#include <net_device.h>

#include "CppUtils.h"


class DwmacDriver;


class DwmacNetDevice {
public:
	static status_t InitDevice(const char* name, net_device*& outDevice);
	status_t UninitDevice();

	status_t Up();
	void Down();

	status_t Control(int32 op, void* argument, size_t length);

	status_t SendData(net_buffer* buffer);
	// use net_stack_module_info::device_enqueue_buffer instead
	// status_t ReceiveData(net_buffer*& buffer);

	status_t SetMtu(size_t mtu);
	status_t SetPromiscuous(bool promiscuous);
	status_t SetMedia(uint32 media);

	status_t AddMulticast(const struct sockaddr* address);
	status_t RemoveMulticast(const struct sockaddr* address);

	void ReleaseDriver() {fDriver = NULL;}

	static DwmacNetDevice* FromNetDevice(net_device* dev) {return &ContainerOf(*dev, &DwmacNetDevice::fNetDev);}
	net_device* ToNetDevice() {return &fNetDev;}

private:
	net_device fNetDev {};
	uint32	fFrameSize {};

	DwmacDriver* fDriver {};

	inline status_t InitDeviceInt(DwmacDriver* driver);
};
