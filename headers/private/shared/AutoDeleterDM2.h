#pragma once

#include <AutoDeleter.h>

#include <dm2/device_manager.h>


typedef MethodDeleter<DeviceNode, int32, &DeviceNode::ReleaseReference> DeviceNodePutter;
