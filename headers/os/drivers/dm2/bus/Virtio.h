#pragma once

#include <dm2/device_manager.h>
#include <KernelExport.h>


#define VIRTIO_DEVICE_ID_NETWORK	1
#define VIRTIO_DEVICE_ID_BLOCK		2
#define VIRTIO_DEVICE_ID_CONSOLE	3
#define VIRTIO_DEVICE_ID_ENTROPY	4
#define VIRTIO_DEVICE_ID_BALLOON	5
#define VIRTIO_DEVICE_ID_IOMEMORY	6
#define VIRTIO_DEVICE_ID_RP_MESSAGE	7
#define VIRTIO_DEVICE_ID_SCSI		8
#define VIRTIO_DEVICE_ID_9P			9
#define VIRTIO_DEVICE_ID_RP_SERIAL	11
#define VIRTIO_DEVICE_ID_CAIF		12
#define VIRTIO_DEVICE_ID_GPU		16
#define VIRTIO_DEVICE_ID_INPUT		18
#define VIRTIO_DEVICE_ID_VSOCK		19
#define VIRTIO_DEVICE_ID_CRYPTO		20

#define VIRTIO_FEATURE_TRANSPORT_MASK	((1 << 28) - 1)

#define VIRTIO_FEATURE_NOTIFY_ON_EMPTY		(1 << 24)
#define VIRTIO_FEATURE_ANY_LAYOUT			(1 << 27)
#define VIRTIO_FEATURE_RING_INDIRECT_DESC	(1 << 28)
#define VIRTIO_FEATURE_RING_EVENT_IDX		(1 << 29)
#define VIRTIO_FEATURE_BAD_FEATURE			(1 << 30)

#define VIRTIO_VIRTQUEUES_MAX_COUNT	8

#define VIRTIO_CONFIG_STATUS_RESET		0x00
#define VIRTIO_CONFIG_STATUS_ACK		0x01
#define VIRTIO_CONFIG_STATUS_DRIVER		0x02
#define VIRTIO_CONFIG_STATUS_DRIVER_OK	0x04
#define VIRTIO_CONFIG_STATUS_FAILED		0x80

// attributes:
#define VIRTIO_DEVICE_TYPE_ITEM		"virtio/type"				/* uint16, device type */
#define VIRTIO_VRING_ALIGNMENT_ITEM "virtio/vring_alignment"	/* uint16, alignment */


// callback function for requests
typedef void (*virtio_callback_func)(void* driverCookie, void* cookie);
// callback function for interrupts
typedef void (*virtio_intr_func)(void* cookie);


class VirtioQueue {
public:
	virtual status_t SetupInterrupt(virtio_callback_func handler, void* cookie) = 0;
	virtual status_t Request(const physical_entry* readEntry, const physical_entry* writtenEntry, void* cookie) = 0;
	virtual status_t RequestV(const physical_entry* vector, size_t readVectorCount, size_t writtenVectorCount, void* cookie) = 0;
	virtual bool IsFull() = 0;
	virtual bool IsEmpty() = 0;
	virtual uint16 Size() = 0;
	virtual bool Dequeue(void** _cookie, uint32* _usedLength) = 0;

protected:
	~VirtioQueue() = default;
};


class VirtioDevice {
public:
	static inline const char ifaceName[] = "bus_managers/virtio/device";

	virtual status_t NegotiateFeatures(uint64 supported, uint64* negotiated, const char* (*get_feature_name)(uint64)) = 0;
	virtual status_t ClearFeature(uint64 feature) = 0;

	virtual status_t ReadDeviceConfig(uint8 offset, void* buffer, size_t bufferSize) = 0;
	virtual status_t WriteDeviceConfig(uint8 offset, const void* buffer, size_t bufferSize) = 0;

	virtual status_t AllocQueues(size_t count, VirtioQueue** queues) = 0;
	virtual void FreeQueues() = 0;

	virtual status_t SetupInterrupt(virtio_intr_func config_handler, void* driverCookie) = 0;
	virtual status_t FreeInterrupts() = 0;

protected:
	~VirtioDevice() = default;
};


class VirtioSim {
public:
	virtual status_t QueueInterruptHandler(uint16 queue) = 0;
	virtual status_t ConfigInterruptHandler() = 0;

protected:
	~VirtioSim() = default;
};


class VirtioController {
public:
	virtual void set_sim(VirtioSim* sim) = 0;
	virtual status_t read_host_features(uint32* features) = 0;
	virtual status_t write_guest_features(uint32 features) = 0;
	virtual uint8 get_status() = 0;
	virtual void set_status(uint8 status) = 0;
	virtual status_t read_device_config(uint8 offset, void* buffer, size_t bufferSize) = 0;
	virtual status_t write_device_config(uint8 offset, const void* buffer, size_t bufferSize) = 0;

	virtual uint16 get_queue_ring_size(uint16 queue) = 0;
	virtual status_t setup_queue(uint16 queue, phys_addr_t phy) = 0;
	virtual status_t setup_interrupt(uint16 queueCount) = 0;
	virtual status_t free_interrupt() = 0;
	virtual void notify_queue(uint16 queue) = 0;

protected:
	~VirtioController() = default;
};
