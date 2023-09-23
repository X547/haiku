#include <new>

#include <util/AutoLock.h>
#include <util/Vector.h>

#include "DeviceManager.h"
#include "UserlandInterface2.h"
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

	private:
		dm_device_node_id AllocId(BReference<DeviceNodeImpl> node);
		void FreeId(dm_device_node_id nodeId);

	private:
		DeviceManagerDriver& fBase;
		mutex fLock = MUTEX_INITIALIZER("DeviceManager handle");

		Vector<BReference<DeviceNodeImpl>> fNodeIds;
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
	CHECK_RET(fNode->RegisterDevFsNode("system/device_manager", &fDevFsNode));

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


status_t
DeviceManagerDriver::DevFsNodeHandle::Control(uint32 op, void* buffer, size_t length, bool isKernel)
{
	switch (op) {
		case DM_GET_VERSION: {
			return B_OK;
		};
	}
	return B_DEV_INVALID_IOCTL;
}


dm_device_node_id
DeviceManagerDriver::DevFsNodeHandle::AllocId(BReference<DeviceNodeImpl> node)
{
	MutexLocker lock(&fLock);

	int32 index = fNodeIds.Find(NULL);
	if (index < 0)
		index = fNodeIds.Count();

	CHECK_RET(fNodeIds.Insert(node, index));
	return index;
}


void DeviceManagerDriver::DevFsNodeHandle::FreeId(dm_device_node_id nodeId)
{
	MutexLocker lock(&fLock);

	if (nodeId < 0 || nodeId >= fNodeIds.Count())
		return;

	fNodeIds[nodeId] = NULL;

	while (fNodeIds.Count() > 0 && fNodeIds[fNodeIds.Count() - 1].IsSet())
		fNodeIds.PopBack();
}


driver_module_info gDeviceManagerDriverModule = {
	.info = {
		.name = DEVICE_MANAGER_DRIVER_MODULE_NAME,
	},
	.probe = DeviceManagerDriver::Probe
};
