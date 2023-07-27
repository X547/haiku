#include <dm2/device_manager.h>

#include <Referenceable.h>


class DeviceManager {
public:
};


class DeviceNodeImpl: public DeviceNode, public BReferenceable {
public:
	virtual ~DeviceNodeImpl() = default;

	int32 AcquireReference() final { return BReferenceable::AcquireReference(); }
	int32 ReleaseReference() final { return BReferenceable::ReleaseReference(); }

	DeviceNode* GetParent() const final;
	status_t GetNextChildNode(const device_attr* attrs, DeviceNode** node) const final;
	status_t FindChildNode(const device_attr* attrs, DeviceNode** node) const final;

	status_t GetNextAttr(device_attr** attr) const final;
	status_t FindAttr(const char* name, type_code type, int32 index, const void** value) const final;

	void* QueryBusInterface(const char* ifaceName) final;
	void* QueryDriverInterface(const char* ifaceName) final;

	status_t InstallListener(DeviceNodeListener* listener) final;
	status_t UninstallListener(DeviceNodeListener* listener) final;

	status_t RegisterNode(BusDriver* driver, DeviceNode** node) final;
	status_t UnregisterNode(DeviceNode* node) final;

	status_t RegisterDevFsNode(const char* path, DevFsNode* driver) final;
	status_t UnregisterDevFsNode(const char* path) final;

private:
	DeviceNodeImpl* fParent {};

	DeviceDriver* fDeviceDriver {};
	BusDriver* fBusDriver {};
};


DeviceNode*
DeviceNodeImpl::GetParent() const
{
	if (fParent != NULL)
		fParent->AcquireReference();

	return fParent;
}


status_t
DeviceNodeImpl::GetNextChildNode(const device_attr* attrs, DeviceNode** node) const
{
	// TODO: implement
	return ENOSYS;
}

status_t
DeviceNodeImpl::FindChildNode(const device_attr* attrs, DeviceNode** node) const
{
	// TODO: implement
	return ENOSYS;
}


status_t
DeviceNodeImpl::GetNextAttr(device_attr** attr) const
{
	// TODO: implement
	return ENOSYS;
}


status_t
DeviceNodeImpl::FindAttr(const char* name, type_code type, int32 index, const void** value) const
{
	// TODO: implement
	return ENOSYS;
}


void*
DeviceNodeImpl::QueryBusInterface(const char* ifaceName)
{
	if (fBusDriver == NULL)
		return NULL;

	return fBusDriver->QueryInterface(ifaceName);
}

void*
DeviceNodeImpl::QueryDriverInterface(const char* ifaceName)
{
	if (fDeviceDriver == NULL)
		return NULL;

	return fDeviceDriver->QueryInterface(ifaceName);
}


status_t
DeviceNodeImpl::InstallListener(DeviceNodeListener* listener)
{
	// TODO: implement
	return ENOSYS;
}


status_t
DeviceNodeImpl::UninstallListener(DeviceNodeListener* listener)
{
	// TODO: implement
	return ENOSYS;
}


status_t
DeviceNodeImpl::RegisterNode(BusDriver* driver, DeviceNode** node)
{
	// TODO: implement
	return ENOSYS;
}

status_t
DeviceNodeImpl::UnregisterNode(DeviceNode* node)
{
	// TODO: implement
	return ENOSYS;
}


status_t
DeviceNodeImpl::RegisterDevFsNode(const char* path, DevFsNode* driver)
{
	// TODO: implement
	return ENOSYS;
}

status_t
DeviceNodeImpl::UnregisterDevFsNode(const char* path)
{
	// TODO: implement
	return ENOSYS;
}
