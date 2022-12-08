/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#pragma once

#include "PS2.h"


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define PS2_MODULE_NAME	"drivers/input/ps2_input/driver_v1"

#define PS2_MOUSE_MODULE_NAME	"drivers/input/ps2_input/ps2_mouse/driver_v1"
#define PS2_MOUSE_DEVICE_MODULE_NAME	"drivers/input/ps2_input/ps2_mouse/device/v1"


extern device_manager_info* gDeviceManager;
extern ps2_device_interface gControllerModuleInfo;
extern driver_module_info gMouseModuleInfo;
extern device_module_info gMouseDeviceModuleInfo;
