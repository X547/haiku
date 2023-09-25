#include <algorithm>
#include <new>

#include <errno.h>

#include <dm2/uapi/device_manager.h>

#include <ScopeExit.h>
#include <util/AutoLock.h>
#include <util/Vector.h>

#include "DeviceManager.h"
#include "UserlandInterface2Private.h"


class DeviceManagerDriver: public DeviceDriver {
public:
	DeviceManagerDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~DeviceManagerDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	DeviceNode* fNode;


	class DevFsNodeHandle: public ::DevFsNodeHandle {
	public:
		DevFsNodeHandle(DeviceManagerDriver& base): fBase(base) {}
		virtual ~DevFsNodeHandle() = default;
		void Free() final {delete this;}

		status_t Init();

		status_t Control(uint32 op, void* buffer, size_t length, bool isKernel) final;

		int CreateFd(BReference<DeviceNodeImpl> node);

	private:
		DeviceManagerDriver& fBase;
		mutex fLock = MUTEX_INITIALIZER("DeviceManager handle");

		BReference<DeviceNodeImpl> fNode;
	};


	class DevFsNode: public ::DevFsNode {
	public:
		DevFsNode(DeviceManagerDriver& base): fBase(base) {}

		Capabilities GetCapabilities() const final {return {.control = true};}
		status_t Open(const char* path, int openMode, ::DevFsNodeHandle** outHandle) final;

	private:
		DeviceManagerDriver& fBase;
	} fDevFsNode;
};


status_t
DeviceManagerDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<DeviceManagerDriver> driver(new(std::nothrow) DeviceManagerDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
DeviceManagerDriver::Init()
{
	CHECK_RET(fNode->RegisterDevFsNode(DM_DEVICE_NAME, &fDevFsNode));

	return B_OK;
}


status_t
DeviceManagerDriver::DevFsNode::Open(const char* path, int openMode, ::DevFsNodeHandle** outHandle)
{
	ObjectDeleter<DevFsNodeHandle> handle(new DevFsNodeHandle(fBase));
	CHECK_RET(handle->Init());
	*outHandle = handle.Detach();
	return B_OK;
}


status_t
DeviceManagerDriver::DevFsNodeHandle::Init()
{
	return B_OK;
}


int
DeviceManagerDriver::DevFsNodeHandle::CreateFd(BReference<DeviceNodeImpl> node)
{
	int fd = open("/dev/" DM_DEVICE_NAME, O_RDWR);
	if (fd < 0)
		return errno;

	DevFsNodeHandle* newHandle {};
	ioctl(fd, DM_GET_COOKIE, &newHandle, sizeof(newHandle));

	MutexLocker lock(&newHandle->fLock);
	newHandle->fNode = node;

	return fd;
}


status_t
DeviceManagerDriver::DevFsNodeHandle::Control(uint32 op, void* buffer, size_t length, bool isKernel)
{
	if (isKernel && op == DM_GET_COOKIE) {
		*(DevFsNodeHandle**)buffer = this;
		return B_OK;
	}

	dm_command command;

	size_t copyLength = std::min(sizeof(command), length);

	CHECK_RET(user_memcpy(&command, buffer, copyLength));

	auto DoControl = [this](uint32 op, dm_command& command, size_t length) -> status_t {
		switch (op) {
			case DM_GET_VERSION:
				return DM_PROTOCOL_VERSION;

			case DM_GET_NODE_ID:
				if (!fNode.IsSet())
					return ENOENT;

				return fNode->Id();

			case DM_GET_ROOT_NODE:
				return CreateFd(BReference(DeviceManager::Instance().GetRootNode(), true));

			case DM_GET_CHILD_NODE: {
				if (!fNode.IsSet())
					return ENOENT;

				DeviceNode* childNode = NULL;
				CHECK_RET(fNode->GetNextChildNode(NULL, &childNode));

				return CreateFd(BReference(static_cast<DeviceNodeImpl*>(childNode), true));
			}
			case DM_GET_PARENT_NODE: {
				if (!fNode.IsSet())
					return ENOENT;

				DeviceNode* parentNode = fNode->GetParent();
				if (parentNode == NULL)
					return ENOENT;

				return CreateFd(BReference(static_cast<DeviceNodeImpl*>(parentNode), true));
			}
			case DM_GET_NEXT_NODE: {
				if (!fNode.IsSet())
					return ENOENT;

				DeviceNode* childNode = fNode.Get();
				CHECK_RET(fNode->GetNextChildNode(NULL, &childNode));

				return CreateFd(BReference(static_cast<DeviceNodeImpl*>(childNode), true));
			}
			case DM_GET_ATTR: {
				if (length < sizeof(command.getAttr)) {
					return B_BAD_VALUE;
				}

				if (!fNode.IsSet())
					return ENOENT;

				const device_attr* attr = NULL;
				for (int32 i = 0; i < command.getAttr.index; i++) {
					CHECK_RET(fNode->GetNextAttr(&attr));
				}

				memcpy(&command.getAttr.attr, attr, sizeof(*attr));

				switch (attr->type) {
					case B_STRING_TYPE: {
						command.getAttr.attr.value.string = (char*)command.getAttr.dataBuffer;
						size_t size = strlen(attr->value.string) + 1;
						// !!! userland may provide kernel address
						CHECK_RET(user_memcpy(command.getAttr.dataBuffer, attr->value.string, std::min(size, command.getAttr.dataBufferSize)));
						command.getAttr.dataBufferSize = size;
						break;
					}
					case B_RAW_TYPE: {
						command.getAttr.attr.value.raw.data = command.getAttr.dataBuffer;
						size_t size = attr->value.raw.length;
						// !!! userland may provide kernel address
						CHECK_RET(user_memcpy(command.getAttr.dataBuffer, attr->value.raw.data, std::min(size, command.getAttr.dataBufferSize)));
						command.getAttr.dataBufferSize = size;
						break;
					}
					default:
						command.getAttr.dataBufferSize = 0;
				}

				return B_OK;
			}
		}
		return B_DEV_INVALID_IOCTL;
	};

	status_t res = DoControl(op, command, length);

	CHECK_RET(user_memcpy(buffer, &command, copyLength));

	return res;
}


driver_module_info gDeviceManagerDriverModule = {
	.info = {
		.name = DEVICE_MANAGER_DRIVER_MODULE_NAME,
	},
	.probe = DeviceManagerDriver::Probe
};
