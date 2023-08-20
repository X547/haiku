#pragma once

#include <dm2/bus/MMC.h>

#include <Drivers.h>

#include <AutoDeleterOS.h>

#include "dma_resources.h"
#include "IORequest.h"
#include "IOSchedulerSimple.h"


#define TRACE_MMC_DISK
#ifdef TRACE_MMC_DISK
#	define TRACE(x...) dprintf("\33[33mmmc_disk:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)			dprintf("\33[33mmmc_disk:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class IOScheduler;
class DMAResource;
class MmcDiskDriver;


class MmcDiskDevFsNodeHandle: public DevFsNodeHandle {
public:
	MmcDiskDevFsNodeHandle(MmcDiskDriver& driver): fDriver(driver) {}
	virtual ~MmcDiskDevFsNodeHandle() = default;

	void Free() final {delete this;}
	status_t IO(io_request *request) final;
	status_t Control(uint32 op, void *buffer, size_t length) final;

private:
	MmcDiskDriver& fDriver;
};


class MmcDiskDevFsNode: public DevFsNode {
public:
	MmcDiskDevFsNode(MmcDiskDriver& driver): fDriver(driver) {}
	virtual ~MmcDiskDevFsNode() = default;

	Capabilities GetCapabilities() const final {return {.io = true, .control = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

private:
	MmcDiskDriver& fDriver;
};


class MmcDiskDriver: public DeviceDriver, public IOCallback {
public:
	MmcDiskDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~MmcDiskDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

	// IOCallback
	status_t DoIO(IOOperation* operation) final;

private:
	DeviceNode* fNode {};
	MmcDevice* fMmcDevice {};
	ObjectDeleter<IOScheduler> fIoScheduler;
	ObjectDeleter<DMAResource> fDmaResource;

	MmcDiskDevFsNode fDevFsNode;


	friend class MmcDiskDevFsNode;
	friend class MmcDiskDevFsNodeHandle;


	status_t Init();
	status_t GetGeometry(device_geometry* geometry);
};
