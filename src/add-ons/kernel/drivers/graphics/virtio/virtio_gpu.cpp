/*
 * Copyright 2023, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <atomic>
#include <new>

#include <graphic_driver.h>

#include <util/AutoLock.h>
#include <dm2/bus/Virtio.h>
#include <virtio_info.h>
#include <condition_variable.h>

#include <AutoDeleter.h>
#include <ContainerOf.h>
#include <ScopeExit.h>

#include "viogpu.h"
#include "PhysicalMemoryAllocator.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define VIRTIO_GPU_DRIVER_MODULE_NAME "drivers/graphics/virtio_gpu/driver/v1"


class VirtioGpuDriver: public DeviceDriver {
public:
	VirtioGpuDriver(DeviceNode* node): fNode(node) {}
	virtual ~VirtioGpuDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

	status_t DrainQueues();
	status_t SendCmd(void *cmd, size_t cmdSize, void *response, size_t responseSize);
	status_t GetDisplayInfo();
	status_t GetEdids(int scanout);
	status_t Create2d(int resourceId, int width, int height);
	status_t Unref(int resourceId);
	status_t AttachBacking(int resourceId);
	status_t DetachBacking(int resourceId);
	status_t SetScanout(int scanoutId, int resourceId, uint32 width, uint32 height);
	status_t TransferToHost2d(int resourceId, uint32 width, uint32 height);
	status_t FlushResource(int resourceId, uint32 width, uint32 height);

	static status_t UpdateThread(void *arg);
	static void Vqwait(void* driverCookie, void* cookie);

private:
	DeviceNode*				fNode;
	VirtioDevice* 			fVirtioDevice {};

	PhysicalMemoryAllocator	fPhysMemAllocator {"virtio_gpu", 32, 1024*1024, 4};

	uint64 					fFeatures {};

	VirtioQueue*			fControlQueue {};
	spinlock				fCommandLock = B_SPINLOCK_INITIALIZER;
	uint64					fFenceId {};

	VirtioQueue*			fCursorQueue {};

	int						fDisplayResourceId {};
	uint32					fFramebufferWidth {};
	uint32					fFramebufferHeight {};
	area_id					fFramebufferArea = -1;
	addr_t					fFramebuffer {};
	size_t					fFramebufferSize {};

	thread_id				fUpdateThread = -1;
	bool					fUpdateThreadRunning {};

	area_id					fSharedArea = -1;
	virtio_gpu_shared_info* fSharedInfo {};

	std::atomic<int32>		fOpenCount {};


	class DevFsNode: public ::DevFsNode, public ::DevFsNodeHandle {
	public:
		VirtioGpuDriver& Base() {return ContainerOf(*this, &VirtioGpuDriver::fDevFsNode);}

		Capabilities GetCapabilities() const final {return {.control = true};}
		status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) final;
		status_t Close() final;
		status_t Control(uint32 op, void* buffer, size_t length, bool isKernel) final;
	} fDevFsNode;
};


#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <fs/devfs.h>

#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))


#define DEVICE_NAME				"virtio_gpu"
#define ACCELERANT_NAME	"virtio_gpu.accelerant"
//#define TRACE_VIRTIO_GPU
#ifdef TRACE_VIRTIO_GPU
#	define TRACE(x...) dprintf(DEVICE_NAME ": " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)			dprintf("\33[33m" DEVICE_NAME ":\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


const char*
get_feature_name(uint64 feature)
{
	switch (feature) {
		case VIRTIO_GPU_F_VIRGL:
			return "virgl";
		case VIRTIO_GPU_F_EDID:
			return "edid";
		case VIRTIO_GPU_F_RESOURCE_UUID:
			return "res_uuid";
		case VIRTIO_GPU_F_RESOURCE_BLOB:
			return "res_blob";
	}
	return NULL;
}


status_t
VirtioGpuDriver::DrainQueues()
{
	while (fControlQueue->Dequeue(NULL, NULL))
		;

	while (fCursorQueue->Dequeue(NULL, NULL))
		;

	return B_OK;
}


status_t
VirtioGpuDriver::SendCmd(void *cmd, size_t cmdSize, void *response,
    size_t responseSize)
{
	size_t totalSize = cmdSize + responseSize;
	uint8* cmdVirtAdr;
	phys_addr_t cmdPhysAdr;

	CHECK_RET(fPhysMemAllocator.Allocate(totalSize, (void**)&cmdVirtAdr, &cmdPhysAdr));
	ScopeExit memoryReleaser([this, totalSize, cmdVirtAdr, cmdPhysAdr] {
		fPhysMemAllocator.Deallocate(totalSize, cmdVirtAdr, cmdPhysAdr);
	});

	struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)cmdVirtAdr;
	struct virtio_gpu_ctrl_hdr *responseHdr = (struct virtio_gpu_ctrl_hdr *)response;

	memcpy((void*)cmdVirtAdr, cmd, cmdSize);
	memset((void*)(cmdVirtAdr + cmdSize), 0, responseSize);
	hdr->flags |= VIRTIO_GPU_FLAG_FENCE;
	hdr->fence_id = ++fFenceId;

	physical_entry entries[] {
		{ cmdPhysAdr, cmdSize },
		{ cmdPhysAdr + cmdSize, responseSize },
	};

	ConditionVariable completedCond;
	completedCond.Init(this, "completedCond");
	ConditionVariableEntry cvEntry;
	completedCond.Add(&cvEntry);

	InterruptsSpinLocker lock(&fCommandLock);

	status_t status = fControlQueue->RequestV(entries, 1, 1, &completedCond);
	if (status != B_OK)
		return status;

	lock.Unlock();
	cvEntry.Wait();

	memcpy(response, (void*)(cmdVirtAdr + cmdSize), responseSize);

	if (responseHdr->fence_id != fFenceId) {
		ERROR("response fence id not right(expected: %" B_PRIu64 ", actual: %" B_PRIu64 ")\n", fFenceId, responseHdr->fence_id);
	}
	return B_OK;
}


status_t
VirtioGpuDriver::GetDisplayInfo()
{
	CALLED();
	struct virtio_gpu_ctrl_hdr hdr = {};
	struct virtio_gpu_resp_display_info displayInfo = {};

	hdr.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

	SendCmd(&hdr, sizeof(hdr), &displayInfo, sizeof(displayInfo));

	if (displayInfo.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
		ERROR("failed getting display info\n");
		return B_ERROR;
	}

	if (!displayInfo.pmodes[0].enabled) {
		ERROR("pmodes[0] is not enabled\n");
		return B_BAD_VALUE;
	}

	fFramebufferWidth = displayInfo.pmodes[0].r.width;
	fFramebufferHeight = displayInfo.pmodes[0].r.height;
	TRACE("virtio_gpu_get_display_info width %" B_PRIu32 " height %" B_PRIu32 "\n",
		fFramebufferWidth, fFramebufferHeight);

	return B_OK;
}


status_t
VirtioGpuDriver::GetEdids(int scanout)
{
	CALLED();
	struct virtio_gpu_cmd_get_edid getEdid = {};
	struct virtio_gpu_resp_edid response = {};
	getEdid.hdr.type = VIRTIO_GPU_CMD_GET_EDID;
	getEdid.scanout = scanout;

	SendCmd(&getEdid, sizeof(getEdid), &response, sizeof(response));

	if (response.hdr.type != VIRTIO_GPU_RESP_OK_EDID) {
		ERROR("failed getting edids %d\n", response.hdr.type);
		return B_ERROR;
	}

	fSharedInfo->has_edid = true;
	memcpy(&fSharedInfo->edid_raw, response.edid, sizeof(edid1_raw));

	return B_OK;
}


status_t
VirtioGpuDriver::Create2d(int resourceId, int width, int height)
{
	CALLED();
	struct virtio_gpu_resource_create_2d resource = {};
	struct virtio_gpu_ctrl_hdr response = {};

	resource.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	resource.resource_id = resourceId;
	resource.format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
	resource.width = width;
	resource.height = height;

	SendCmd(&resource, sizeof(resource), &response, sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("viogpu_create_2d: failed %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::Unref(int resourceId)
{
	CALLED();
	struct virtio_gpu_resource_unref resource = {};
	struct virtio_gpu_ctrl_hdr response = {};

	resource.hdr.type = VIRTIO_GPU_CMD_RESOURCE_UNREF;
	resource.resource_id = resourceId;

	SendCmd(&resource, sizeof(resource), &response, sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("virtio_gpu_unref: failed %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::AttachBacking(int resourceId)
{
	CALLED();
	struct virtio_gpu_resource_attach_backing_entries {
		struct virtio_gpu_resource_attach_backing backing;
		struct virtio_gpu_mem_entry entries[16];
	} _PACKED backing = {};
	struct virtio_gpu_ctrl_hdr response = {};

	physical_entry entries[16] = {};
	status_t status = get_memory_map((void*)fFramebuffer, fFramebufferSize, entries, 16);
	if (status != B_OK) {
		ERROR("virtio_gpu_attach_backing get_memory_map failed: %s\n", strerror(status));
		return status;
	}

	backing.backing.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	backing.backing.resource_id = resourceId;
	for (int i = 0; i < 16; i++) {
		if (entries[i].size == 0)
			break;
		TRACE("virtio_gpu_attach_backing %d %lx %lx\n", i, entries[i].address, entries[i].size);
		backing.entries[i].addr = entries[i].address;
		backing.entries[i].length = entries[i].size;
		backing.backing.nr_entries++;
	}

	SendCmd(&backing, sizeof(backing), &response, sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("virtio_gpu_attach_backing failed: %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::DetachBacking(int resourceId)
{
	CALLED();
	struct virtio_gpu_resource_detach_backing backing;
	struct virtio_gpu_ctrl_hdr response = {};

	backing.hdr.type = VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING;
	backing.resource_id = resourceId;

	SendCmd(&backing, sizeof(backing), &response, sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("virtio_gpu_detach_backing failed: %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::SetScanout(int scanoutId, int resourceId,
    uint32 width, uint32 height)
{
	CALLED();
	struct virtio_gpu_set_scanout set_scanout = {};
	struct virtio_gpu_ctrl_hdr response = {};

	set_scanout.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
	set_scanout.scanout_id = scanoutId;
	set_scanout.resource_id = resourceId;
	set_scanout.r.width = width;
	set_scanout.r.height = height;

	SendCmd(&set_scanout, sizeof(set_scanout), &response, sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("virtio_gpu_set_scanout failed %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::TransferToHost2d(int resourceId,
    uint32 width, uint32 height)
{
	struct virtio_gpu_transfer_to_host_2d transferToHost = {};
	struct virtio_gpu_ctrl_hdr response = {};

	transferToHost.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	transferToHost.resource_id = resourceId;
	transferToHost.r.width = width;
	transferToHost.r.height = height;

	SendCmd(&transferToHost, sizeof(transferToHost), &response,
		sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("virtio_gpu_transfer_to_host_2d failed %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::FlushResource(int resourceId, uint32 width,
    uint32 height)
{
	struct virtio_gpu_resource_flush resourceFlush = {};
	struct virtio_gpu_ctrl_hdr response = {};

	resourceFlush.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	resourceFlush.resource_id = resourceId;
	resourceFlush.r.width = width;
	resourceFlush.r.height = height;

	SendCmd(&resourceFlush, sizeof(resourceFlush), &response, sizeof(response));

	if (response.type != VIRTIO_GPU_RESP_OK_NODATA) {
		ERROR("virtio_gpu_flush_resource failed %d\n", response.type);
		return B_ERROR;
	}

	return B_OK;
}


status_t
VirtioGpuDriver::UpdateThread(void *arg)
{
	VirtioGpuDriver* info = (VirtioGpuDriver*)arg;

	while (info->fUpdateThreadRunning) {
		bigtime_t start = system_time();
		info->TransferToHost2d(info->fDisplayResourceId, info->fFramebufferWidth,
			info->fFramebufferHeight);
		info->FlushResource(info->fDisplayResourceId, info->fFramebufferWidth, info->fFramebufferHeight);
		bigtime_t delay = system_time() - start;
		if (delay < 20000)
			snooze(20000 - delay);
	}
	return B_OK;
}


//	#pragma mark - device module API


status_t
VirtioGpuDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<VirtioGpuDriver> driver(new(std::nothrow) VirtioGpuDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
VirtioGpuDriver::Init()
{
	fVirtioDevice = fNode->QueryBusInterface<VirtioDevice>();

	CHECK_RET(fPhysMemAllocator.InitCheck());

	fVirtioDevice->NegotiateFeatures(/*VIRTIO_GPU_F_EDID*/0,
		 &fFeatures, &get_feature_name);

	// TODO read config

	// Setup queues
	VirtioQueue* virtioQueues[2];
	CHECK_RET(fVirtioDevice->AllocQueues(2, virtioQueues));

	fControlQueue = virtioQueues[0];
	fCursorQueue = virtioQueues[1];

	// Setup interrupt
	CHECK_RET(fVirtioDevice->SetupInterrupt(NULL, this));
	CHECK_RET(fControlQueue->SetupInterrupt(Vqwait, this));

	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), "graphics/virtio/%" B_PRId32, id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


VirtioGpuDriver::~VirtioGpuDriver()
{
	CALLED();

	fVirtioDevice->FreeInterrupts();

	fVirtioDevice->FreeQueues();
}


status_t
VirtioGpuDriver::DevFsNode::Open(const char* path, int openMode, DevFsNodeHandle** outHandle)
{
	CALLED();

	int32 oldOpenCount = Base().fOpenCount.fetch_add(1);
	if (oldOpenCount >= 1) {
		*outHandle = this;
		return B_OK;
	}

	status_t status;
	size_t sharedSize = (sizeof(virtio_gpu_shared_info) + 7) & ~7;

	status = Base().GetDisplayInfo();
	if (status != B_OK)
		goto error;

	// create framebuffer area
	Base().fFramebufferSize = 4 * Base().fFramebufferWidth * Base().fFramebufferHeight;
	Base().fFramebufferArea = create_area("virtio_gpu framebuffer", (void**)&Base().fFramebuffer,
		B_ANY_KERNEL_ADDRESS, Base().fFramebufferSize,
		B_FULL_LOCK | B_CONTIGUOUS, B_READ_AREA | B_WRITE_AREA);
	if (Base().fFramebufferArea < B_OK) {
		status = Base().fFramebufferArea;
		goto error;
	}

	Base().fDisplayResourceId = 1;
	status = Base().Create2d(Base().fDisplayResourceId, Base().fFramebufferWidth,
		Base().fFramebufferHeight);
	if (status != B_OK)
		goto error2;

	status = Base().AttachBacking(Base().fDisplayResourceId);
	if (status != B_OK)
		goto error2;

	status = Base().SetScanout(0, Base().fDisplayResourceId, Base().fFramebufferWidth,
		Base().fFramebufferHeight);
	if (status != B_OK)
		goto error2;

	Base().fSharedArea = create_area("virtio_gpu shared info",
		(void**)&Base().fSharedInfo, B_ANY_KERNEL_ADDRESS,
		ROUND_TO_PAGE_SIZE(sharedSize), B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (Base().fSharedArea < 0)
		goto error4;

	{
		virtio_gpu_shared_info& sharedInfo = *Base().fSharedInfo;

		memset(&sharedInfo, 0, sizeof(virtio_gpu_shared_info));

		sharedInfo.frame_buffer_area = Base().fFramebufferArea;
		sharedInfo.frame_buffer = (uint8*)Base().fFramebuffer;
		sharedInfo.bytes_per_row = Base().fFramebufferWidth * 4;

		sharedInfo.current_mode.virtual_width = Base().fFramebufferWidth;
		sharedInfo.current_mode.virtual_height = Base().fFramebufferHeight;
		sharedInfo.current_mode.space = B_RGB32;

		if ((Base().fFeatures & VIRTIO_GPU_F_EDID) != 0)
			Base().GetEdids(0);
	}

	Base().fUpdateThreadRunning = true;
	Base().fUpdateThread = spawn_kernel_thread(UpdateThread, "virtio_gpu update",
		B_DISPLAY_PRIORITY, &Base());
	if (Base().fUpdateThread < B_OK)
		goto error3;
	resume_thread(Base().fUpdateThread);

	*outHandle = this;
	return B_OK;

error4:
error3:
error2:
	delete_area(Base().fFramebufferArea);
	Base().fFramebufferArea = -1;
error:
	return B_ERROR;
}


status_t
VirtioGpuDriver::DevFsNode::Close()
{
	CALLED();

	if (Base().fOpenCount.fetch_add(1) > 1)
		return B_OK;

	Base().fUpdateThreadRunning = false;

	int32 result;
	wait_for_thread(Base().fUpdateThread, &result);
	Base().fUpdateThread = -1;
	//Base().DrainQueues();

	return B_OK;
}


void
VirtioGpuDriver::Vqwait(void* driverCookie, void* cookie)
{
	CALLED();
	VirtioGpuDriver* info = (VirtioGpuDriver*)cookie;

	SpinLocker lock(&info->fCommandLock);

	ConditionVariable* cv;
	if (info->fControlQueue->Dequeue((void**)&cv, NULL))
		cv->NotifyAll();
}


status_t
VirtioGpuDriver::DevFsNode::Control(uint32 op, void* buffer, size_t length, bool isKernel)
{
	CALLED();

	// TRACE("ioctl(op = %lx)\n", op);

	switch (op) {
		case B_GET_ACCELERANT_SIGNATURE:
			dprintf(DEVICE_NAME ": acc: %s\n", ACCELERANT_NAME);
			if (user_strlcpy((char*)buffer, ACCELERANT_NAME,
					B_FILE_NAME_LENGTH) < B_OK)
				return B_BAD_ADDRESS;

			return B_OK;

		// needed to share data between kernel and accelerant
		case VIRTIO_GPU_GET_PRIVATE_DATA:
			return user_memcpy(buffer, &Base().fSharedArea, sizeof(area_id));

		default:
			ERROR("ioctl: unknown message %" B_PRIx32 "\n", op);
			break;
	}

	return B_DEV_INVALID_IOCTL;
}


//	#pragma mark -


static driver_module_info sVirtioGpuDriverModule = {
	.info = {
		.name = VIRTIO_GPU_DRIVER_MODULE_NAME,
	},
	.probe = VirtioGpuDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sVirtioGpuDriverModule,
	NULL
};
