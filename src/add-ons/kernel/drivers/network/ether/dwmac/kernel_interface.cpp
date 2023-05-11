#include "kernel_interface.h"
#include "DwmacDriver.h"
#include "DwmacNetDevice.h"

#include <new>


device_manager_info* gDeviceManager;
net_stack_module_info* gStackModule;
net_buffer_module_info* gBufferModule;


static status_t
dwmac_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			new(&DwmacRoster::Instance()) DwmacRoster();
			return B_OK;

		case B_MODULE_UNINIT:
			DwmacRoster::Instance().~DwmacRoster();
			return B_OK;
	}
	return B_ERROR;
}


struct driver_module_info sDriverModule = {
	.info = {
		.name = DWMAC_DRIVER_MODULE_NAME,
		.std_ops = dwmac_std_ops
	},
	.supports_device = DwmacDriver::SupportsDevice,
	.register_device = DwmacDriver::RegisterDevice,
	.init_driver = [](device_node* node, void** driverCookie) {
		return DwmacDriver::InitDriver(node,
			*(DwmacDriver**)driverCookie);
	},
	.uninit_driver = [](void* driverCookie) {
		return static_cast<DwmacDriver*>(driverCookie)->UninitDriver();
	},
	.register_child_devices = [](void* driverCookie) {
		return static_cast<DwmacDriver*>(driverCookie)->RegisterChildDevices();
	},
};

struct device_module_info sDeviceModule = {
	.info = {
		.name = DWMAC_DEVICE_MODULE_NAME
	},
	.init_device = [](void *driverCookie, void **deviceCookie) {*deviceCookie = driverCookie; return B_OK;},
	.uninit_device = [](void *deviceCookie) {},
	.open = [](void *deviceCookie, const char *path, int openMode, void **cookie) {*cookie = deviceCookie; return B_OK;},
	.close = [](void *cookie) {return B_OK;},
	.free = [](void *cookie) {return B_OK;},
	.control = [](void *cookie, uint32 op, void *buffer, size_t length) {return B_DEV_INVALID_IOCTL;}
};

static net_device_module_info sNetDeviceModule = {
	.info = {
		.name = DWMAC_NET_DEVICE_MODULE_NAME
	},
	.init_device = [](const char* name, net_device** _device) {
		return DwmacNetDevice::InitDevice(name, *(DwmacNetDevice**)_device);
	},
	.uninit_device = [](net_device* device) {
		return static_cast<DwmacNetDevice*>(device)->UninitDevice();
	},
	.up = [](net_device* device) {
		return static_cast<DwmacNetDevice*>(device)->Up();
	},
	.down = [](net_device* device) {
		static_cast<DwmacNetDevice*>(device)->Down();
	},
	.control = [](net_device* device, int32 op, void* argument, size_t length) {
		return static_cast<DwmacNetDevice*>(device)->Control(op, argument, length);
	},
	.send_data = [](net_device* device, net_buffer* buffer) {
		return static_cast<DwmacNetDevice*>(device)->SendData(buffer);
	},
	.receive_data = [](net_device* device, net_buffer** buffer) {
		return static_cast<DwmacNetDevice*>(device)->ReceiveData(*buffer);
	},
	.set_mtu = [](net_device* device, size_t mtu) {
		return static_cast<DwmacNetDevice*>(device)->SetMtu(mtu);
	},
	.set_promiscuous = [](net_device* device, bool promiscuous) {
		return static_cast<DwmacNetDevice*>(device)->SetPromiscuous(promiscuous);
	},
	.set_media = [](net_device* device, uint32 media) {
		return static_cast<DwmacNetDevice*>(device)->SetMedia(media);
	},
	.add_multicast = [](net_device* device, const struct sockaddr* address) {
		return static_cast<DwmacNetDevice*>(device)->AddMulticast(address);
	},
	.remove_multicast = [](net_device* device, const struct sockaddr* address) {
		return static_cast<DwmacNetDevice*>(device)->RemoveMulticast(address);
	},
};


_EXPORT module_dependency module_dependencies[] = {
	{B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager},
	{NET_STACK_MODULE_NAME, (module_info **)&gStackModule},
	{NET_BUFFER_MODULE_NAME, (module_info **)&gBufferModule},
	{}
};

_EXPORT module_info* modules[] = {
	(module_info *)&sDriverModule,
	(module_info *)&sDeviceModule,
	(module_info *)&sNetDeviceModule,
	NULL
};
