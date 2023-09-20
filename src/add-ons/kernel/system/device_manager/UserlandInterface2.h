#pragma once

#include <Drivers.h>


#define B_DM_PROTOCOL_VERSION 1


typedef int32 dm_device_node_id;


enum {
	DM_GET_VERSION = B_DEVICE_OP_CODES_END + 1,

	DM_CLOSE_NODE,
	DM_GET_ROOT_NODE,
	DM_GET_CHILD_NODE,
	DM_GET_PARENT_NODE,
	DM_GET_NEXT_NODE,

	DM_GET_DRIVER_MODULE_NAME,
	DM_ENABLE_DRIVER,
	DM_DISABLE_DRIVER,
	DM_RESTART_DRIVER,
	DM_REPROBE_DRIVER,

	DM_GET_FIRST_ATTR,
	DM_GET_NEXT_ATTR,
};


union dm_command {
	struct {
		status_t status;
	} version;

	struct {
		dm_device_node_id nodeId;
	} node;
};
