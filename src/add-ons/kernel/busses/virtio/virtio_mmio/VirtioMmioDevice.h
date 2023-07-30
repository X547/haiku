/*
 * Copyright 2021-2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _VIRTIODEVICE_H_
#define _VIRTIODEVICE_H_


#include <virtio_defs.h>
#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <Referenceable.h>
#include <util/AutoLock.h>
#include <util/Bitmap.h>
#include <util/Vector.h>

#include <dm2/bus/Virtio.h>


#define TRACE_VIRTIO
#ifdef TRACE_VIRTIO
#	define TRACE(x...) dprintf("virtio_mmio: " x)
#else
#	define TRACE(x...) ;
#endif

#define TRACE_ALWAYS(x...) dprintf("virtio_mmio: " x)
#define ERROR(x...) dprintf("virtio_mmio: " x)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


struct VirtioIrqHandler;
struct VirtioMmioDevice;


class VirtioMmioQueue: public VirtioQueue {
private:
	VirtioMmioDevice *fDev {};
	int32 fId {};
	size_t fQueueLen {};
	size_t fDescCount {};
	AreaDeleter fArea;
	volatile VirtioDesc  *fDescs {};
	volatile VirtioAvail *fAvail {};
	volatile VirtioUsed  *fUsed {};
	Bitmap fAllocatedDescs;
	uint16 fLastUsed {};
	ArrayDeleter<void*> fCookies;

	BReference<VirtioIrqHandler> fQueueHandlerRef;
	virtio_callback_func fQueueHandler {};
	void *fQueueHandlerCookie {};


	int32 AllocDesc();
	void FreeDesc(int32 idx);

	friend class VirtioMmioDevice;
	friend class VirtioIrqHandler;

public:
	VirtioMmioQueue(VirtioMmioDevice *dev, int32 id);
	virtual ~VirtioMmioQueue() = default;
	status_t Init();

	status_t SetupInterrupt(virtio_callback_func handler, void* cookie) final;
	status_t Request(const physical_entry* readEntry, const physical_entry* writtenEntry, void* cookie) final;
	status_t RequestV(const physical_entry* vector, size_t readVectorCount, size_t writtenVectorCount, void* cookie) final;
	bool IsFull() final;
	bool IsEmpty() final;
	uint16 Size() final;
	bool Dequeue(void** _cookie, uint32* _usedLength) final;
};


struct VirtioIrqHandler: public BReferenceable {
	VirtioMmioDevice* fDev;

	VirtioIrqHandler(VirtioMmioDevice* dev);

	void FirstReferenceAcquired() final;
	void LastReferenceReleased() final;

	static int32 Handle(void* data);
};


class VirtioMmioDevice: public VirtioDevice {
private:
	AreaDeleter fRegsArea;
	volatile VirtioRegs *fRegs;
	int32 fIrq;
	int32 fQueueCnt;
	ArrayDeleter<ObjectDeleter<VirtioMmioQueue> > fQueues;

	VirtioIrqHandler fIrqHandler;

	BReference<VirtioIrqHandler> fConfigHandlerRef;
	virtio_intr_func fConfigHandler;
	void* fConfigHandlerCookie;

	friend class VirtioMmioQueue;
	friend class VirtioIrqHandler;
	friend class VirtioMmioBusDriver; // !!!

public:
	VirtioMmioDevice();
	status_t Init(phys_addr_t regs, size_t regsLen, int32 irq, int32 queueCnt);

	status_t NegotiateFeatures(uint32 supported, uint32* negotiated, const char* (*get_feature_name)(uint32)) final;
	status_t ClearFeature(uint32 feature) final;

	status_t ReadDeviceConfig(uint8 offset, void* buffer, size_t bufferSize) final;
	status_t WriteDeviceConfig(uint8 offset, const void* buffer, size_t bufferSize) final;

	status_t AllocQueues(size_t count, VirtioQueue** queues) final;
	void FreeQueues() final;

	status_t SetupInterrupt(virtio_intr_func config_handler, void* driverCookie) final;
	status_t FreeInterrupts() final;
};


class VirtioMmioDeviceDriver: public DeviceDriver {
public:
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);

	virtual ~VirtioMmioDeviceDriver() = default;

	virtual void Free();

private:
	DeviceNode* fNode {};
	VirtioMmioDevice fDevice;

	VirtioMmioDeviceDriver(DeviceNode* node): fNode(node) {}
	status_t Init();
};


class VirtioMmioBusDriver: public BusDriver {
public:
	VirtioMmioBusDriver(VirtioMmioDevice& device): fDevice(device) {}
	virtual ~VirtioMmioBusDriver() = default;

	void Free() final;
	status_t InitDriver(DeviceNode* node) final;
	const device_attr* Attributes() const final;
	void* QueryInterface(const char* name) final;

private:
	VirtioMmioDevice& fDevice;
	Vector<device_attr> fAttrs;
};


#endif	// _VIRTIODEVICE_H_
