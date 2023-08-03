#include <new>

#include <dm2/device_manager.h>

#include <AutoDeleter.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define NULL_DRIVER_MODULE_NAME "drivers/null/driver/v1"


class NullDriver;


class NullDevFsNode: public DevFsNode, public DevFsNodeHandle {
public:
	NullDevFsNode(NullDriver& driver): fDriver(driver) {}
	virtual ~NullDevFsNode() = default;

	Capabilities GetCapabilities() const final {return {.read = true, .write = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) final;
	status_t Read(off_t pos, void* buffer, size_t* _length) final;
	status_t Write(off_t pos, const void* buffer, size_t* _length) final;

private:
	NullDriver& fDriver;
};


class NullDriver: public DeviceDriver {
public:
	NullDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~NullDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;

private:
	status_t Init();

private:
	DeviceNode* fNode {};
	NullDevFsNode fDevFsNode;
};


// #pragma mark - NullDevFsNode

status_t
NullDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle** outHandle)
{
	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
NullDevFsNode::Read(off_t pos, void* buffer, size_t* _length)
{
	*_length = 0;
	return B_OK;
}


status_t
NullDevFsNode::Write(off_t pos, const void* buffer, size_t* _length)
{
	return B_OK;
}


// #pragma mark - NullDriver

status_t
NullDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<NullDriver> driver(new(std::nothrow) NullDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


void
NullDriver::Free()
{
	delete this;
}


status_t
NullDriver::Init()
{
	CHECK_RET(fNode->RegisterDevFsNode("null", &fDevFsNode));

	return B_OK;
}


static driver_module_info sNullModuleInfo = {
	.info = {
		.name = NULL_DRIVER_MODULE_NAME,
	},
	.probe = NullDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sNullModuleInfo,
	NULL
};
