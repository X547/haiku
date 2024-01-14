#include <new>

#include <KernelExport.h>
#include <dm2/device_manager.h>

#include <AutoDeleter.h>
#include <util/AutoLock.h>
#include <ContainerOf.h>

#include <kernel.h>
#include <generic_syscall.h>
#include <lock.h>

#include <random_defs.h>

#include "yarrow_rng.h"


static mutex sRandomLock = MUTEX_INITIALIZER("RandomDriver");


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define RANDOM_DRIVER_MODULE_NAME "drivers/random/driver/v1"


class RandomDriver: public DeviceDriver {
public:
	RandomDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~RandomDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;

private:
	status_t Init();

private:
	DeviceNode* fNode {};

	class DevFsNode: public ::DevFsNode, public DevFsNodeHandle {
	public:
		RandomDriver& Base() {return ContainerOf(*this, &RandomDriver::fDevFsNode);}

		DevFsNode(RandomDriver& driver): fDriver(driver) {}
		virtual ~DevFsNode() = default;

		Capabilities GetCapabilities() const final {return {.read = true, .write = true};}
		status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) final;
		status_t Read(off_t pos, void* buffer, size_t* _length) final;
		status_t Write(off_t pos, const void* buffer, size_t* _length) final;

	private:
		RandomDriver& fDriver;
	} fDevFsNode;
};


// #pragma mark - RandomDriver

status_t
RandomDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<RandomDriver> driver(new(std::nothrow) RandomDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


void
RandomDriver::Free()
{
	delete this;
}


status_t
RandomDriver::Init()
{
	CHECK_RET(fNode->RegisterDevFsNode("random", &fDevFsNode));
	CHECK_RET(fNode->RegisterDevFsNode("urandom", &fDevFsNode));

	return B_OK;
}


// #pragma mark - RandomDriver::DevFsNode

status_t
RandomDriver::DevFsNode::Open(const char* path, int openMode, DevFsNodeHandle** outHandle)
{
	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
RandomDriver::DevFsNode::Read(off_t pos, void* buffer, size_t* _length)
{
	MutexLocker locker(&sRandomLock);
	return RANDOM_READ(buffer, _length);
}


status_t
RandomDriver::DevFsNode::Write(off_t pos, const void* buffer, size_t* _length)
{
	MutexLocker locker(&sRandomLock);
	return RANDOM_WRITE(buffer, _length);
}


// #pragma mark -

static status_t
random_generic_syscall(const char* subsystem, uint32 function, void* buffer,
	size_t bufferSize)
{
	switch (function) {
		case RANDOM_GET_ENTROPY:
		{
			random_get_entropy_args args;
			if (bufferSize != sizeof(args) || !IS_USER_ADDRESS(buffer))
				return B_BAD_VALUE;

			if (user_memcpy(&args, buffer, sizeof(args)) != B_OK)
				return B_BAD_ADDRESS;
			if (!IS_USER_ADDRESS(args.buffer))
				return B_BAD_ADDRESS;

			{
				MutexLocker locker(&sRandomLock);
				CHECK_RET(RANDOM_READ(args.buffer, &args.length));
			}

			return user_memcpy(buffer, &args, sizeof(args));
		}
	}
	return B_BAD_HANDLER;
}


static status_t
random_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			RANDOM_INIT();
			register_generic_syscall(RANDOM_SYSCALLS, random_generic_syscall, 1, 0);
			return B_OK;

		case B_MODULE_UNINIT:
			unregister_generic_syscall(RANDOM_SYSCALLS, 1);
			RANDOM_UNINIT();
			return B_OK;

		default:
			return B_ERROR;
	}
}


static driver_module_info sRandomModuleInfo = {
	.info = {
		.name = RANDOM_DRIVER_MODULE_NAME,
		.std_ops = random_std_ops
	},
	.probe = RandomDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sRandomModuleInfo,
	NULL
};
