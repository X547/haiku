#include <new>

#include <KernelExport.h>
#include <dm2/device_manager.h>

#include <AutoDeleter.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define ZERO_DRIVER_MODULE_NAME "drivers/zero/driver/v1"


class ZeroDriver;


class ZeroDevFsNode: public DevFsNode, public DevFsNodeHandle {
public:
	ZeroDevFsNode(ZeroDriver& driver): fDriver(driver) {}
	virtual ~ZeroDevFsNode() = default;

	Capabilities GetCapabilities() const final {return {.read = true, .write = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) final;
	status_t Read(off_t pos, void* buffer, size_t* _length) final;
	status_t Write(off_t pos, const void* buffer, size_t* _length) final;

private:
	ZeroDriver& fDriver;
};


class ZeroDriver: public DeviceDriver {
public:
	ZeroDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~ZeroDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;

private:
	status_t Init();

private:
	DeviceNode* fNode {};
	ZeroDevFsNode fDevFsNode;
};


// #pragma mark - ZeroDevFsNode

status_t
ZeroDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle** outHandle)
{
	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
ZeroDevFsNode::Read(off_t pos, void* buffer, size_t* _length)
{
	if (user_memset(buffer, 0, *_length) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


status_t
ZeroDevFsNode::Write(off_t pos, const void* buffer, size_t* _length)
{
	return B_OK;
}


// #pragma mark - ZeroDriver

status_t
ZeroDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<ZeroDriver> driver(new(std::nothrow) ZeroDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


void
ZeroDriver::Free()
{
	delete this;
}


status_t
ZeroDriver::Init()
{
	CHECK_RET(fNode->RegisterDevFsNode("zero", &fDevFsNode));

	return B_OK;
}


static driver_module_info sZeroModuleInfo = {
	.info = {
		.name = ZERO_DRIVER_MODULE_NAME,
	},
	.probe = ZeroDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sZeroModuleInfo,
	NULL
};
