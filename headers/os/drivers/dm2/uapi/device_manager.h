#pragma once

#include <Drivers.h>

#include <dm2/device_manager.h>


#define DM_DEVICE_NAME "system/device_manager"

#define DM_PROTOCOL_VERSION 1


typedef int32 dm_device_node_id;


enum {
	DM_GET_VERSION = B_DEVICE_OP_CODES_END + 1,

	DM_GET_COOKIE, // kernel private

	DM_GET_NODE_ID,
	DM_GET_ROOT_NODE,
	DM_GET_CHILD_NODE,
	DM_GET_PARENT_NODE,
	DM_GET_NEXT_NODE,

	DM_GET_DRIVER_MODULE_NAME,
	DM_ENABLE_DRIVER,
	DM_DISABLE_DRIVER,
	DM_RESTART_DRIVER,
	DM_REPROBE_DRIVER,

	DM_GET_ATTR,
};


union dm_command {
	struct {
		char* name;
		size_t nameLen;
	} getDriverModuleName;

	struct {
		int32 index;
		void* dataBuffer;
		size_t dataBufferSize;
		device_attr attr;
	} getAttr;
};
