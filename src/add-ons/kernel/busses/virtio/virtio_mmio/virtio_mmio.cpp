/*
 * Copyright 2013, 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include <ByteOrder.h>
#include <KernelExport.h>
#include <device_manager.h>

#include <drivers/bus/FDT.h>

#include <debug.h>

#include <virtio.h>
#include <Virtio.h>
#include "VirtioDevice.h"


#define TRACE_VIRTIO
#ifdef TRACE_VIRTIO
#	define TRACE(x...) dprintf("virtio_mmio: " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("virtio_mmio: " x)
#define ERROR(x...)			dprintf("virtio_mmio: " x)


#define VIRTIO_MMIO_DEVICE_MODULE_NAME "busses/virtio/virtio_mmio/driver_v1"

#define VIRTIO_MMIO_CONTROLLER_TYPE_NAME "virtio MMIO controller"


device_manager_info* gDeviceManager;


//#pragma mark Device

static float
virtio_device_supports_device(device_node* parent)
{
	TRACE("supports_device(%p)\n", parent);

	const char* name;
	const char* bus;
	const char* compatible;
	
	if (gDeviceManager->get_attr_string(parent, B_DEVICE_PRETTY_NAME, &name, false) >= B_OK)
		dprintf("  name: %s\n", name);

	if (
		gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) < B_OK ||
		gDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false) < B_OK
	) {
		return -1.0f;
	}

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	if (strcmp(compatible, "virtio,mmio") != 0)
		return 0.0f;

	return 1.0f;
}


static status_t
virtio_device_register_device(device_node* parent)
{
	TRACE("register_device(%p)\n", parent);

	fdt_device_module_info *parentModule;
	fdt_device* parentDev;
	if (gDeviceManager->get_driver(parent, (driver_module_info**)&parentModule, (void**)&parentDev)) {
		ERROR("can't get parent node driver");
		return B_ERROR;
	}

	uint64 regs, regsLen;
	if (!parentModule->get_reg(parentDev, 0, &regs, &regsLen)) {
		ERROR("no regs");
		return B_ERROR;
	}	

	VirtioRegs *volatile mappedRegs;
	AreaDeleter fRegsArea(map_physical_memory(
		"Virtio MMIO",
		regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void **)&mappedRegs
	));
	if (!fRegsArea.IsSet()) {
		ERROR("cant't map regs");
		return B_ERROR;
	}

	if (mappedRegs->signature != virtioSignature) {
		ERROR("bad signature: 0x%08" B_PRIx32 ", should be 0x%08" B_PRIx32 "\n", mappedRegs->signature, virtioSignature);
		return B_ERROR;
	}

	dprintf("  version: 0x%08" B_PRIx32 "\n",   mappedRegs->version);
	dprintf("  deviceId: 0x%08" B_PRIx32 "\n",  mappedRegs->deviceId);
	dprintf("  vendorId: 0x%08" B_PRIx32 "\n",  mappedRegs->vendorId);

	device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Virtio MMIO"}},
		{B_DEVICE_BUS,         B_STRING_TYPE, {string: "virtio"}},
		{"virtio/version",     B_UINT32_TYPE, {ui32: mappedRegs->version}},
		{"virtio/device_id",   B_UINT32_TYPE, {ui32: mappedRegs->deviceId}},
		{"virtio/type",        B_UINT16_TYPE, {ui16: (uint16)mappedRegs->deviceId}},
		{"virtio/vendor_id",   B_UINT32_TYPE, {ui32: mappedRegs->vendorId}},
		{}
	};

	return gDeviceManager->register_node(parent, VIRTIO_MMIO_DEVICE_MODULE_NAME,
		attrs, NULL, NULL);
}


static status_t
virtio_device_init_device(device_node* node, void** cookie)
{
	TRACE("init_device(%p)\n", node);

	device_node* parent = gDeviceManager->get_parent_node(node);
	fdt_device_module_info *parentModule;
	fdt_device* parentDev;
	if (gDeviceManager->get_driver(parent, (driver_module_info**)&parentModule, (void**)&parentDev))
		panic("can't get parent node driver");

	dprintf("  bus: %p\n", parentModule->get_bus(parentDev));
	dprintf("  compatible: %s\n", (const char*)parentModule->get_prop(parentDev, "compatible", NULL));

	uint64 regs;
	uint64 regsLen;
	for (uint32 i = 0; parentModule->get_reg(parentDev, i, &regs, &regsLen); i++) {
		dprintf("  reg[%" B_PRIu32 "]: (0x%" B_PRIx64 ", 0x%" B_PRIx64 ")\n", i, regs, regsLen);
	}
	
	device_node* interruptController;
	uint64 interrupt;
	for (uint32 i = 0; parentModule->get_interrupt(parentDev, i, &interruptController, &interrupt); i++) {
		const char* name;
		if (
			interruptController == NULL ||
			gDeviceManager->get_attr_string(interruptController, "fdt/name", &name, false) < B_OK
		) name = NULL;
		dprintf("  interrupt[%" B_PRIu32 "]: ('%s', 0x%" B_PRIx64 ")\n", i, name, interrupt);
	}
	
	if (!parentModule->get_reg(parentDev, 0, &regs, &regsLen)) {
		dprintf("  no regs\n");
		return B_ERROR;
	}
	
	if (!parentModule->get_interrupt(parentDev, 0, &interruptController, &interrupt)) {
		dprintf("  no interrupts\n");
		return B_ERROR;
	}
	
	ObjectDeleter<VirtioDevice> dev(new(std::nothrow) VirtioDevice());
	if (!dev.IsSet())
		return B_NO_MEMORY;

	dev->Init(regs, regsLen, interrupt, 1);

	*cookie = dev.Detach(); 
	return B_OK;
}


static void
virtio_device_uninit_device(void* cookie)
{
	TRACE("uninit_device(%p)\n", cookie);
	ObjectDeleter<VirtioDevice> dev((VirtioDevice*)cookie);
}


static status_t
virtio_device_register_child_devices(void* cookie)
{
	TRACE("register_child_devices(%p)\n", cookie);
	return B_OK;
}


//#pragma mark driver API

static const char *
virtio_get_feature_name(uint32 feature)
{
	switch (feature) {
		case VIRTIO_FEATURE_NOTIFY_ON_EMPTY:
			return "notify on empty";
		case VIRTIO_FEATURE_RING_INDIRECT_DESC:
			return "ring indirect";
		case VIRTIO_FEATURE_RING_EVENT_IDX:
			return "ring event index";
		case VIRTIO_FEATURE_BAD_FEATURE:
			return "bad feature";
	}
	return NULL;
}

static void
DumpFeatures(const char* title, uint32 features,
	const char* (*get_feature_name)(uint32))
{
	char features_string[512] = "";
	for (uint32 i = 0; i < 32; i++) {
		uint32 feature = features & (1 << i);
		if (feature == 0)
			continue;
		const char* name = virtio_get_feature_name(feature);
		if (name == NULL)
			name = get_feature_name(feature);
		if (name != NULL) {
			strlcat(features_string, "[", sizeof(features_string));
			strlcat(features_string, name, sizeof(features_string));
			strlcat(features_string, "] ", sizeof(features_string));
		}
	}
	TRACE("%s: %s\n", title, features_string);
}

static status_t
virtio_device_negotiate_features(virtio_device cookie, uint32 supported,
	uint32* negotiated, const char* (*get_feature_name)(uint32))
{
	TRACE("virtio_device_negotiate_features(%p)\n", cookie);
	VirtioDevice* dev = (VirtioDevice*)cookie;

	dev->fRegs->status |= virtioConfigSAcknowledge;
	dev->fRegs->status |= virtioConfigSDriver;

	uint32 features = dev->fRegs->deviceFeatures;
	DumpFeatures("read features", features, get_feature_name);
	features &= supported;
	// filter our own features
	features &= (VIRTIO_FEATURE_TRANSPORT_MASK | VIRTIO_FEATURE_RING_INDIRECT_DESC | VIRTIO_FEATURE_RING_EVENT_IDX);
	*negotiated = features;
	DumpFeatures("negotiated features", features, get_feature_name);
	dev->fRegs->driverFeatures = features;

	dev->fRegs->status |= virtioConfigSFeaturesOk;
	dev->fRegs->status |= virtioConfigSDriverOk;

	dev->fRegs->guestPageSize = B_PAGE_SIZE;

	return B_OK;
}

static status_t
virtio_device_clear_feature(virtio_device cookie, uint32 feature)
{
	panic("not implemented");
	return B_ERROR;
}

static status_t
virtio_device_read_device_config(virtio_device cookie, uint8 offset,
	void* buffer, size_t bufferSize)
{
	TRACE("virtio_device_read_device_config(%p, %d, %" B_PRIuSIZE ")\n", cookie, offset, bufferSize);
	VirtioDevice* dev = (VirtioDevice*)cookie;
	memcpy(buffer, dev->fRegs->config + offset, bufferSize);
	return B_OK;
}

static status_t
virtio_device_write_device_config(virtio_device cookie, uint8 offset,
	const void* buffer, size_t bufferSize)
{
	TRACE("virtio_device_write_device_config(%p, %d, %" B_PRIuSIZE ")\n", cookie, offset, bufferSize);
	VirtioDevice* dev = (VirtioDevice*)cookie;
	memcpy(dev->fRegs->config + offset, buffer, bufferSize);
	return B_OK;
}

static status_t
virtio_device_alloc_queues(virtio_device cookie, size_t count,
	virtio_queue* queues)
{
	TRACE("virtio_device_alloc_queues(%p, %" B_PRIuSIZE ")\n", cookie, count);
	VirtioDevice* dev = (VirtioDevice*)cookie;

	dev->fQueueCnt = count;
	dev->fQueues = new(std::nothrow) VirtioQueue*[dev->fQueueCnt];
	for (int32_t i = 0; i < dev->fQueueCnt; i++) {
		dev->fQueues[i] = new(std::nothrow) VirtioQueue(dev, i);
		queues[i] = dev->fQueues[i];
	}
	
	return B_OK;
}

static void
virtio_device_free_queues(virtio_device cookie)
{
	TRACE("virtio_device_free_queues(%p)\n", cookie);
	VirtioDevice* dev = (VirtioDevice*)cookie;

	for (int32_t i = 0; i < dev->fQueueCnt; i++)
		delete dev->fQueues[i];

	delete[] dev->fQueues;
	dev->fQueues = NULL;
	dev->fQueueCnt = 0;
}

static int32
virtio_interrupt_handler(void *data)
{
	// TRACE("virtio_interrupt_handler(%p)\n", data);
	VirtioDevice* dev = (VirtioDevice*)data;
	dev->fRegs->interruptAck = dev->fRegs->interruptStatus & 0x3;

	dev->fQueueHandler(NULL /* ? */, dev->fQueueHandlerCookie);

	return B_HANDLED_INTERRUPT;
}


void WritePC(addr_t adr);

static status_t
virtio_device_setup_interrupt(virtio_device cookie,
	virtio_intr_func config_handler, void* driverCookie)
{
	TRACE("virtio_device_setup_interrupt(%p, ", cookie);
	WritePC((addr_t)config_handler);
	dprintf("): not implemented\n");

	// panic("not implemented");
	return B_OK;
}

static status_t
virtio_device_free_interrupts(virtio_device cookie)
{
	TRACE("virtio_device_free_interrupts(%p): not implemened\n", cookie);
	// panic("not implemented");
	return B_OK;
}

static status_t
virtio_device_queue_setup_interrupt(virtio_queue aQueue,
	virtio_callback_func handler, void* cookie)
{
	TRACE("virtio_device_queue_setup_interrupt(%p, ", aQueue);
	WritePC((addr_t)handler);
	dprintf(")\n");

	VirtioQueue* queue = (VirtioQueue*)aQueue;
	VirtioDevice* dev = queue->fDev;
	MutexLocker lock(dev->fLock);

	dev->fQueueHandler = handler;
	dev->fQueueHandlerCookie = cookie;

	install_io_interrupt_handler(dev->fIrq, virtio_interrupt_handler, dev, 0);

	// panic("not implemented");
	return B_OK;
}

static status_t
virtio_device_queue_request_v(virtio_queue aQueue,
	const physical_entry* vector,
	size_t readVectorCount, size_t writtenVectorCount,
	void* cookie)
{
	// TRACE("virtio_device_queue_request_v(%p, %" B_PRIuSIZE ", %" B_PRIuSIZE ", %p)\n", aQueue, readVectorCount, writtenVectorCount, cookie);
	VirtioQueue* queue = (VirtioQueue*)aQueue;
	VirtioDevice* dev = queue->fDev;
	MutexLocker lock(dev->fLock);

	int32 firstDesc, lastDesc;
	size_t count = readVectorCount + writtenVectorCount;
	if (count == 0)
		return B_OK;

	for (size_t i = 0; i < count; i++) {
		// dprintf("  vector[%" B_PRIuSIZE "]: 0x%" B_PRIx64 ", 0x%" B_PRIx64 ", %s\n", i, vector[i].address, vector[i].size, (i < readVectorCount) ? "read" : "write");
		int32 desc = queue->AllocDesc();
		// dprintf("%p.AllocDesc(): %" B_PRId32 "\n", queue, desc);
		if (desc < 0) {
			panic("no free virtio descs, queue: %p\n", queue);

			desc = firstDesc;
			while (vringDescFlagsNext & desc) {
				int32_t nextDesc = queue->fDescs[desc].next;
				// dprintf("%p.FreeDesc(): %" B_PRId32 "\n", queue, desc);
				queue->FreeDesc(desc);
				desc = nextDesc;
			}

			// dprintf("%p.FreeDesc(): %" B_PRId32 "\n", queue, desc);
			queue->FreeDesc(desc);

			return B_WOULD_BLOCK;
/*
			lock.Unset();
			thread_yield();
			return virtio_device_queue_request_v(aQueue, vector, readVectorCount, writtenVectorCount, cookie);
*/
		}
		if (i == 0) {
			firstDesc = desc;
		} else {
			queue->fDescs[lastDesc].flags |= vringDescFlagsNext;
			queue->fDescs[lastDesc].next = desc;
		}
		queue->fDescs[desc].addr = vector[i].address;
		queue->fDescs[desc].len = vector[i].size;
		queue->fDescs[desc].flags = 0;
		queue->fDescs[desc].next = 0;
		if (i >= readVectorCount)
			queue->fDescs[desc].flags |= vringDescFlagsWrite;

		lastDesc = desc;
	}

	int32_t idx = queue->fAvail->idx % queue->fQueueLen;
	queue->fReqs[idx] = (VirtioRequest*)cookie; // !!!
	queue->fAvail->ring[idx] = firstDesc;
	queue->fAvail->idx++;
	dev->fRegs->queueNotify = queue->fId;

	return B_OK;
}

static status_t
virtio_device_queue_request(virtio_queue queue,
	const physical_entry* readEntry,
	const physical_entry* writtenEntry, void* cookie)
{
	physical_entry vector[2];
	physical_entry* vectorEnd = vector;
	if (readEntry != NULL)    *vectorEnd++ = *readEntry;
	if (writtenEntry != NULL) *vectorEnd++ = *writtenEntry;
	return virtio_device_queue_request_v(
		queue, vector,
		(readEntry != NULL) ? 1 : 0,
		(writtenEntry != NULL) ? 1 : 0,
		cookie
	);
}

static bool
virtio_device_queue_is_full(virtio_queue queue)
{
	panic("not implemented");
	return false;
}

static bool
virtio_device_queue_is_empty(virtio_queue aQueue)
{
	VirtioQueue *queue = (VirtioQueue *)aQueue;
	MutexLocker lock(queue->fDev->fLock);
	return queue->fUsed->idx == queue->fLastUsed;
}

static uint16
virtio_device_queue_size(virtio_queue aQueue)
{
	VirtioQueue *queue = (VirtioQueue *)aQueue;
	MutexLocker lock(queue->fDev->fLock);
	return (uint16)queue->fQueueLen;
}

static bool
virtio_device_queue_dequeue(virtio_queue aQueue, void** _cookie,
	uint32* _usedLength)
{
	// TRACE("virtio_device_queue_dequeue(%p)\n", aQueue);
	VirtioQueue* queue = (VirtioQueue*)aQueue;
	VirtioDevice* dev = queue->fDev;
	// MutexLocker lock(dev->fLock);

	dev->fRegs->queueSel = queue->fId;

	if (queue->fUsed->idx == queue->fLastUsed) return false;

	if (_cookie != NULL)
		*_cookie = queue->fReqs[queue->fLastUsed % queue->fQueueLen]; // !!!	
	queue->fReqs[queue->fLastUsed % queue->fQueueLen] = NULL;

	if (_usedLength != NULL)
		*_usedLength = queue->fUsed->ring[queue->fLastUsed % queue->fQueueLen].len;

	int32_t desc = queue->fUsed->ring[queue->fLastUsed % queue->fQueueLen].id;
	while (vringDescFlagsNext & queue->fDescs[desc].flags) {
		int32_t nextDesc = queue->fDescs[desc].next;
		// dprintf("%p.FreeDesc(): %" B_PRId32 "\n", queue, desc);
		queue->FreeDesc(desc);
		desc = nextDesc;
	}
	// dprintf("%p.FreeDesc(): %" B_PRId32 "\n", queue, desc);
	queue->FreeDesc(desc);
	queue->fLastUsed++;

	return true;
}


//#pragma mark -

module_dependency module_dependencies[] = {
	{B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager},
	{}
};


static virtio_device_interface sVirtioDevice = {
	{
		{
			VIRTIO_MMIO_DEVICE_MODULE_NAME,
			0,
			NULL
		},
	
		virtio_device_supports_device,
		virtio_device_register_device,
		virtio_device_init_device,
		virtio_device_uninit_device,
		virtio_device_register_child_devices,
		NULL,	// rescan
		NULL,	// device removed
	},
	virtio_device_negotiate_features,
	virtio_device_clear_feature,
	virtio_device_read_device_config,
	virtio_device_write_device_config,
	virtio_device_alloc_queues,
	virtio_device_free_queues,
	virtio_device_setup_interrupt,
	virtio_device_free_interrupts,
	virtio_device_queue_setup_interrupt,
	virtio_device_queue_request,
	virtio_device_queue_request_v,
	virtio_device_queue_is_full,
	virtio_device_queue_is_empty,
	virtio_device_queue_size,
	virtio_device_queue_dequeue,
};

module_info* modules[] = {
	(module_info* )&sVirtioDevice,
	NULL
};

/*

VirtIO block driver use
	string B_DEVICE_BUS == "virtio" 
	uint16 "virtio/type" == 2

	virtio_device_interface
*/
