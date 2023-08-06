#include <kernel.h>
#include <device_manager_defs.h>
#include <generic_syscall.h>

#include "DeviceManager.h"


static status_t
control_device_manager(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	// TODO: this function passes pointers to userland, and uses pointers
	// to device nodes that came from userland - this is completely unsafe
	// and should be changed.
	switch (function) {
		case DM_GET_ROOT:
		{
			device_node_cookie cookie;
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_node_cookie))
				return B_BAD_VALUE;

			DeviceNode* rootNode = DeviceManager::Instance().GetRootNode();
			rootNode->ReleaseReference(); // !!!
			cookie = (device_node_cookie)rootNode;

			// copy back to user space
			return user_memcpy(buffer, &cookie, sizeof(device_node_cookie));
		}

		case DM_GET_CHILD:
		{
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_node_cookie))
				return B_BAD_VALUE;

			device_node_cookie cookie;
			if (user_memcpy(&cookie, buffer, sizeof(device_node_cookie)) < B_OK)
				return B_BAD_ADDRESS;

			DeviceNode* parent = (DeviceNode*)cookie;
			DeviceNode* node = NULL;
			if (parent->GetNextChildNode(NULL, &node) < B_OK)
				return B_ENTRY_NOT_FOUND;

			node->ReleaseReference(); // !!!
			cookie = (device_node_cookie)node;

			// copy back to user space
			return user_memcpy(buffer, &cookie, sizeof(device_node_cookie));
		}

		case DM_GET_NEXT_CHILD:
		{
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_node_cookie))
				return B_BAD_VALUE;

			device_node_cookie cookie;
			if (user_memcpy(&cookie, buffer, sizeof(device_node_cookie)) < B_OK)
				return B_BAD_ADDRESS;

			DeviceNode* node = (DeviceNode*)cookie;
			DeviceNode* parent = node->GetParent();
			if (parent == NULL)
				return B_ENTRY_NOT_FOUND;

			parent->ReleaseReference(); // !!!

			if (parent->GetNextChildNode(NULL, &node) < B_OK)
				return B_ENTRY_NOT_FOUND;

			node->ReleaseReference(); // !!!
			cookie = (device_node_cookie)node;

			// copy back to user space
			return user_memcpy(buffer, &cookie, sizeof(device_node_cookie));
		}

		case DM_GET_NEXT_ATTRIBUTE:
		{
			struct device_attr_info attrInfo;
			if (!IS_USER_ADDRESS(buffer))
				return B_BAD_ADDRESS;
			if (bufferSize != sizeof(device_attr_info))
				return B_BAD_VALUE;
			if (user_memcpy(&attrInfo, buffer, sizeof(device_attr_info)) < B_OK)
				return B_BAD_ADDRESS;

			DeviceNode* node = (DeviceNode*)attrInfo.node_cookie;
			const device_attr* last = (const device_attr*)attrInfo.cookie;

			const device_attr* attr = last;
			if (node->GetNextAttr(&attr) < B_OK)
				return B_ENTRY_NOT_FOUND;

			attrInfo.cookie = (device_node_cookie)attr;
			if (attr->name != NULL)
				strlcpy(attrInfo.name, attr->name, 254);
			else
				attrInfo.name[0] = '\0';
			attrInfo.type = attr->type;
			switch (attrInfo.type) {
				case B_UINT8_TYPE:
					attrInfo.value.ui8 = attr->value.ui8;
					break;
				case B_UINT16_TYPE:
					attrInfo.value.ui16 = attr->value.ui16;
					break;
				case B_UINT32_TYPE:
					attrInfo.value.ui32 = attr->value.ui32;
					break;
				case B_UINT64_TYPE:
					attrInfo.value.ui64 = attr->value.ui64;
					break;
				case B_STRING_TYPE:
					if (attr->value.string != NULL)
						strlcpy(attrInfo.value.string, attr->value.string, 254);
					else
						attrInfo.value.string[0] = '\0';
					break;
				/*case B_RAW_TYPE:
					if (attr.value.raw.length > attr_info->attr.value.raw.length)
						attr.value.raw.length = attr_info->attr.value.raw.length;
					user_memcpy(attr.value.raw.data, attr_info->attr.value.raw.data,
						attr.value.raw.length);
					break;*/
			}

			// copy back to user space
			return user_memcpy(buffer, &attrInfo, sizeof(device_attr_info));
		}
	}

	return B_BAD_HANDLER;
}


status_t
device_manager_install_userland_iface()
{
	return register_generic_syscall(DEVICE_MANAGER_SYSCALLS, control_device_manager, 1, 0);
}


status_t
device_manager_uninstall_userland_iface()
{
	return unregister_generic_syscall(DEVICE_MANAGER_SYSCALLS, 1);
}
