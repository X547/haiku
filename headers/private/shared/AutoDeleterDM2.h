#pragma once

#include <AutoDeleter.h>

#include <dm2/device_manager.h>


struct DeviceNodePutter : MethodDeleter<DeviceNode, int32, &DeviceNode::ReleaseReference>
{
	typedef MethodDeleter<DeviceNode, int32, &DeviceNode::ReleaseReference> Base;

	DeviceNodePutter() : Base() {}
	DeviceNodePutter(DeviceNode* object) : Base(object) {}
};
