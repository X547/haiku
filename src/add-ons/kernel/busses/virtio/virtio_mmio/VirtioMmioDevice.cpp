/*
 * Copyright 2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioMmioDevice.h"

#include <malloc.h>
#include <string.h>
#include <new>

#include <KernelExport.h>
#include <kernel.h>
#include <debug.h>


static const char *
virtio_get_feature_name(uint32 feature)
{
	switch (feature) {
		case VIRTIO_FEATURE_NOTIFY_ON_EMPTY:
			return "notify on empty";
		case VIRTIO_FEATURE_ANY_LAYOUT:
			return "any layout";
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
virtio_dump_features(const char* title, uint32 features,
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




// #pragma mark - VirtioIrqHandler


VirtioIrqHandler::VirtioIrqHandler(VirtioMmioDevice* dev)
	:
	fDev(dev)
{
	fReferenceCount = 0;
}


void
VirtioIrqHandler::FirstReferenceAcquired()
{
	install_io_interrupt_handler(fDev->fIrq, Handle, fDev, 0);
}


void
VirtioIrqHandler::LastReferenceReleased()
{
	remove_io_interrupt_handler(fDev->fIrq, Handle, fDev);
}


int32
VirtioIrqHandler::Handle(void* data)
{
	// TRACE("VirtioIrqHandler::Handle(%p)\n", data);
	VirtioMmioDevice* dev = (VirtioMmioDevice*)data;

	if ((kVirtioIntQueue & dev->fRegs->interruptStatus) != 0) {
		for (int32 i = 0; i < dev->fQueueCnt; i++) {
			VirtioMmioQueue* queue = dev->fQueues[i].Get();
			if (queue->fUsed->idx != queue->fLastUsed
				&& queue->fQueueHandler != NULL) {
				queue->fQueueHandler(dev->fConfigHandlerCookie,
					queue->fQueueHandlerCookie);
				}
		}
		dev->fRegs->interruptAck = kVirtioIntQueue;
	}

	if ((kVirtioIntConfig & dev->fRegs->interruptStatus) != 0) {
		if (dev->fConfigHandler != NULL)
			dev->fConfigHandler(dev->fConfigHandlerCookie);

		dev->fRegs->interruptAck = kVirtioIntConfig;
	}

	return B_HANDLED_INTERRUPT;
}


// #pragma mark - VirtioMmioDevice


VirtioMmioDevice::VirtioMmioDevice()
	:
	fRegs(NULL),
	fQueueCnt(0),
	fIrqHandler(this),
	fConfigHandler(NULL),
	fConfigHandlerCookie(NULL)
{
}


status_t
VirtioMmioDevice::Init(phys_addr_t regs, size_t regsLen, int32 irq, int32 queueCnt)
{
	fRegsArea.SetTo(map_physical_memory("Virtio MMIO",
		regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&fRegs));

	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	fIrq = irq;

	// Reset
	fRegs->status = 0;

	return B_OK;
}


// #pragma mark - Public driver interface

status_t
VirtioMmioDevice::NegotiateFeatures(uint32 supported, uint32* negotiated, const char* (*get_feature_name)(uint32))
{
	TRACE("virtio_device_negotiate_features(%p)\n", this);

	fRegs->status |= kVirtioConfigSAcknowledge;
	fRegs->status |= kVirtioConfigSDriver;

	uint32 features = fRegs->deviceFeatures;
	virtio_dump_features("read features", features, get_feature_name);
	features &= supported;

	// filter our own features
	features &= (VIRTIO_FEATURE_TRANSPORT_MASK
		| VIRTIO_FEATURE_RING_INDIRECT_DESC | VIRTIO_FEATURE_RING_EVENT_IDX);
	*negotiated = features;

	virtio_dump_features("negotiated features", features, get_feature_name);

	fRegs->driverFeatures = features;
	fRegs->status |= kVirtioConfigSFeaturesOk;
	fRegs->status |= kVirtioConfigSDriverOk;
	fRegs->guestPageSize = B_PAGE_SIZE;

	return B_OK;
}


status_t
VirtioMmioDevice::ClearFeature(uint32 feature)
{
	panic("not implemented");
	return B_ERROR;
}


status_t
VirtioMmioDevice::ReadDeviceConfig(uint8 offset, void* buffer, size_t bufferSize)
{
	TRACE("virtio_device_read_device_config(%p, %d, %" B_PRIuSIZE ")\n", this,
		offset, bufferSize);

	// TODO: check ARM support, ARM seems support only 32 bit aligned MMIO access.
	vuint8* src = &fRegs->config[offset];
	uint8* dst = (uint8*)buffer;
	while (bufferSize > 0) {
		uint8 size = 4;
		if (bufferSize == 1) {
			size = 1;
			*dst = *src;
		} else if (bufferSize <= 3) {
			size = 2;
			*(uint16*)dst = *(vuint16*)src;
		} else
			*(uint32*)dst = *(vuint32*)src;

		dst += size;
		bufferSize -= size;
		src += size;
	}

	return B_OK;
}


status_t
VirtioMmioDevice::WriteDeviceConfig(uint8 offset, const void* buffer, size_t bufferSize)
{
	TRACE("virtio_device_write_device_config(%p, %d, %" B_PRIuSIZE ")\n",
		this, offset, bufferSize);

	// See virtio_device_read_device_config
	uint8* src = (uint8*)buffer;
	vuint8* dst = &fRegs->config[offset];
	while (bufferSize > 0) {
		uint8 size = 4;
		if (bufferSize == 1) {
			size = 1;
			*dst = *src;
		} else if (bufferSize <= 3) {
			size = 2;
			*(vuint16*)dst = *(uint16*)src;
		} else
			*(vuint32*)dst = *(uint32*)src;

		dst += size;
		bufferSize -= size;
		src += size;
	}

	return B_OK;
}


status_t
VirtioMmioDevice::AllocQueues(size_t count, VirtioQueue** queues)
{
	TRACE("virtio_device_alloc_queues(%p, %" B_PRIuSIZE ")\n", this, count);

	ArrayDeleter<ObjectDeleter<VirtioMmioQueue> > newQueues(new(std::nothrow)
		ObjectDeleter<VirtioMmioQueue>[count]);

	if (!newQueues.IsSet())
		return B_NO_MEMORY;

	for (size_t i = 0; i < count; i++) {
		newQueues[i].SetTo(new(std::nothrow) VirtioMmioQueue(this, i));

		if (!newQueues[i].IsSet())
			return B_NO_MEMORY;

		status_t res = newQueues[i]->Init();
		if (res < B_OK)
			return res;
	}

	fQueueCnt = count;
	fQueues.SetTo(newQueues.Detach());

	for (size_t i = 0; i < count; i++)
		queues[i] = fQueues[i].Get();

	return B_OK;
}


void
VirtioMmioDevice::FreeQueues()
{
	TRACE("virtio_device_free_queues(%p)\n", this);

	fQueues.Unset();
	fQueueCnt = 0;
}


status_t
VirtioMmioDevice::SetupInterrupt(virtio_intr_func config_handler, void* driverCookie)
{
	TRACE("virtio_device_setup_interrupt(%p, %#" B_PRIxADDR ")\n", this, (addr_t)config_handler);

	fConfigHandler = config_handler;
	fConfigHandlerCookie = driverCookie;
	fConfigHandlerRef.SetTo((config_handler == NULL) ? NULL : &fIrqHandler);

	return B_OK;
}


status_t
VirtioMmioDevice::FreeInterrupts()
{
	TRACE("virtio_device_free_interrupts(%p)\n", this);

	for (int32 i = 0; i < fQueueCnt; i++) {
		VirtioMmioQueue* queue = fQueues[i].Get();
		queue->fQueueHandler = NULL;
		queue->fQueueHandlerCookie = NULL;
		queue->fQueueHandlerRef.Unset();
	}

	fConfigHandler = NULL;
	fConfigHandlerCookie = NULL;
	fConfigHandlerRef.Unset();

	return B_OK;
}
