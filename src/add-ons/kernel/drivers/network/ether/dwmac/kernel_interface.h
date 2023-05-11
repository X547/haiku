#pragma once

#include <device_manager.h>
#include <net_stack.h>
#include <net_buffer.h>


#define DWMAC_DRIVER_MODULE_NAME "drivers/network/dwmac/driver_v1"
#define DWMAC_DEVICE_MODULE_NAME "drivers/network/dwmac/device/v1"
#define DWMAC_NET_DEVICE_MODULE_NAME "network/devices/dwmac/v1"


extern device_manager_info* gDeviceManager;
extern net_stack_module_info* gStackModule;
extern net_buffer_module_info* gBufferModule;
