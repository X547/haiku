#pragma once

#include <dm2/bus/Virtio.h>

#include <Drivers.h>

#include <AutoDeleterOS.h>

#include "dma_resources.h"
#include "IORequest.h"
#include "IOSchedulerSimple.h"

#include "virtio_blk.h"


//#define TRACE_VIRTIO_BLOCK
#ifdef TRACE_VIRTIO_BLOCK
#	define TRACE(x...) dprintf("virtio_block: " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)			dprintf("\33[33mvirtio_block:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class IOScheduler;
class DMAResource;
class VirtioBlockDriver;


class VirtioBlockDevFsNodeHandle: public DevFsNodeHandle {
public:
	VirtioBlockDevFsNodeHandle(VirtioBlockDriver& driver): fDriver(driver) {}
	virtual ~VirtioBlockDevFsNodeHandle() = default;

	void Free() final;
	status_t IO(io_request *request) final;
	status_t Control(uint32 op, void *buffer, size_t length) final;

private:
	VirtioBlockDriver& fDriver;
};


class VirtioBlockDevFsNode: public DevFsNode {
public:
	VirtioBlockDevFsNode(VirtioBlockDriver& driver): fDriver(driver) {}
	virtual ~VirtioBlockDevFsNode() = default;

	Capabilities GetCapabilities() const final {return {.io = true, .control = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

private:
	VirtioBlockDriver& fDriver;
};


class VirtioBlockDriver: public DeviceDriver, public IOCallback {
public:
	VirtioBlockDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~VirtioBlockDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;
	status_t RegisterChildDevices() final;

	// IOCallback
	status_t DoIO(IOOperation* operation) final;

private:
	DeviceNode* fNode {};
	VirtioDevice* fVirtioDevice {};
	VirtioQueue* fVirtioQueue {};
	ObjectDeleter<IOScheduler> fIoScheduler;
	ObjectDeleter<DMAResource> fDmaResource;

	struct virtio_blk_config fConfig {};

	uint32 fFeatures {};
	uint64 fCapacity {};
	uint32 fBlockSize {};
	uint32 fPhysicalBlockSize {};
	status_t fMediaStatus {};

	SemDeleter fSemCb;

	VirtioBlockDevFsNode fDevFsNode;


	friend class VirtioBlockDevFsNode;
	friend class VirtioBlockDevFsNodeHandle;


	status_t Init();
	status_t GetGeometry(device_geometry* geometry);
	bool SetCapacity();

	static void ConfigCallback(void* driverCookie);
	static void Callback(void* driverCookie, void* cookie);
};
