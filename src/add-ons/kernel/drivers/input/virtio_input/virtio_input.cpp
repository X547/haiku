/*
 * Copyright 2013, Jérôme Duval, korli@users.berlios.de.
 * Copyright 2021-2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <stdio.h>
#include <new>

#include <dm2/device_manager.h>
#include <dm2/bus/Virtio.h>

#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#include <util/AutoLock.h>
#include <condition_variable.h>
#include <kernel.h>

#include <virtio_defs.h>
#include <virtio_input_driver.h>


//#define TRACE_VIRTIO_INPUT
#ifdef TRACE_VIRTIO_INPUT
#	define TRACE(x...) dprintf("virtio_input: " x)
#else
#	define TRACE(x...) ;
#endif
#define ERROR(x...)			dprintf("virtio_input: " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}

#define VIRTIO_INPUT_DRIVER_MODULE_NAME "drivers/input/virtio_input/driver/v1"


class VirtioInputDriver;


struct Packet {
	VirtioInputPacket data;
};


class PacketQueue {
private:
	spinlock fLock = B_SPINLOCK_INITIALIZER;

	uint32 fPacketCnt {};

	ArrayDeleter<Packet*> fReadyPackets;
	uint32 fReadyPacketRptr {};
	uint32 fReadyPacketWptr {};

	AreaDeleter fPacketArea;
	phys_addr_t fPhysAdr {};
	Packet* fPackets {};

	ConditionVariable fCanReadCond;

public:
	// `count` must be power of 2
	status_t Init(uint32 count);

	uint32 PacketCount() const { return fPacketCnt; }
	Packet* PacketAt(uint32 index) { return &fPackets[index]; }
	const physical_entry PacketPhysEntry(Packet* pkt) const;

	void Write(Packet* pkt);
	status_t Read(Packet*& pkt);
};


class VirtioInputDevFsNodeHandle: public DevFsNodeHandle {
public:
	VirtioInputDevFsNodeHandle(VirtioInputDriver& driver): fDriver(driver) {}
	virtual ~VirtioInputDevFsNodeHandle() = default;

	void Free() final;
	status_t Control(uint32 op, void* buffer, size_t length) final;

private:
	VirtioInputDriver& fDriver;
};


class VirtioInputDevFsNode: public DevFsNode {
public:
	VirtioInputDevFsNode(VirtioInputDriver& driver): fDriver(driver) {}
	virtual ~VirtioInputDevFsNode() = default;

	Capabilities GetCapabilities() const final {return {.control = true};}
	status_t Open(const char* path, int openMode, DevFsNodeHandle** outHandle) final;

private:
	VirtioInputDriver& fDriver;
};


class VirtioInputDriver: public DeviceDriver {
public:
	VirtioInputDriver(DeviceNode* node): fNode(node), fDevFsNode(*this) {}
	virtual ~VirtioInputDriver() = default;

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final;
	status_t RegisterChildDevices() final;

private:
	status_t Init();
	static void InterruptCallback(void* driverCookie, void* cookie);

private:
	friend class VirtioInputDevFsNodeHandle;

	DeviceNode* fNode {};
	VirtioInputDevFsNode fDevFsNode;

	mutex fVirtioQueueLock = MUTEX_INITIALIZER("virtioQueue");
	VirtioDevice* fVirtioDevice {};
	VirtioQueue* fVirtioQueue {};

	uint32 fFeatures {};

	PacketQueue fPacketQueue;
};


#ifdef TRACE_VIRTIO_INPUT
static void
WriteInputPacket(const VirtioInputPacket &pkt)
{
	switch (pkt.type) {
		case kVirtioInputEvSyn:
			TRACE("syn");
			break;
		case kVirtioInputEvKey:
			TRACE("key, ");
			switch (pkt.code) {
				case kVirtioInputBtnLeft:
					TRACE("left");
					break;
				case kVirtioInputBtnRight:
					TRACE("middle");
					break;
				case kVirtioInputBtnMiddle:
					TRACE("right");
					break;
				case kVirtioInputBtnGearDown:
					TRACE("gearDown");
					break;
				case kVirtioInputBtnGearUp:
					TRACE("gearUp");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvRel:
			TRACE("rel, ");
			switch (pkt.code) {
				case kVirtioInputRelX:
					TRACE("relX");
					break;
				case kVirtioInputRelY:
					TRACE("relY");
					break;
				case kVirtioInputRelZ:
					TRACE("relZ");
					break;
				case kVirtioInputRelWheel:
					TRACE("relWheel");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvAbs:
			TRACE("abs, ");
			switch (pkt.code) {
				case kVirtioInputAbsX:
					TRACE("absX");
					break;
				case kVirtioInputAbsY:
					TRACE("absY");
					break;
				case kVirtioInputAbsZ:
					TRACE("absZ");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvRep:
			TRACE("rep");
			break;
		default:
			TRACE("?(%d)", pkt.type);
	}
	switch (pkt.type) {
		case kVirtioInputEvSyn:
			break;
		case kVirtioInputEvKey:
			TRACE(", ");
			if (pkt.value == 0) {
				TRACE("up");
			} else if (pkt.value == 1) {
				TRACE("down");
			} else {
				TRACE("%" B_PRId32, pkt.value);
			}
			break;
		default:
			TRACE(", %" B_PRId32, pkt.value);
	}
}
#endif


// #pragma mark - PacketQueue

status_t PacketQueue::Init(uint32 count)
{
	fReadyPackets.SetTo(new(std::nothrow) Packet*[count]);
	if (!fReadyPackets.IsSet())
		return B_NO_MEMORY;

	size_t size = ROUNDUP(sizeof(Packet)*count, B_PAGE_SIZE);

	fPacketArea.SetTo(create_area("VirtIO input packets",
		(void**)&fPackets, B_ANY_KERNEL_ADDRESS, size,
		B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));
	if (!fPacketArea.IsSet()) {
		ERROR("Unable to set packet area!");
		return fPacketArea.Get();
	}

	physical_entry pe;
	status_t res = get_memory_map(fPackets, size, &pe, 1);
	if (res < B_OK) {
		ERROR("Unable to get memory map for input packets!");
		return res;
	}
	fPhysAdr = pe.address;
	memset(fPackets, 0, size);
	TRACE("  size: 0x%" B_PRIxSIZE "\n", size);
	TRACE("  virt: %p\n", packets);
	TRACE("  phys: %p\n", (void*)physAdr);

	fPacketCnt = count;

	fCanReadCond.Init(this, "hasReadyPacket");

	return B_OK;
}


const physical_entry
PacketQueue::PacketPhysEntry(Packet* pkt) const
{
	physical_entry pe {
		.address = fPhysAdr + ((uint8*)pkt - (uint8*)fPackets),
		.size = sizeof(VirtioInputPacket)
	};
	return pe;
}


void
PacketQueue::Write(Packet* pkt)
{
	InterruptsSpinLocker lock(&fLock);

#ifdef TRACE_VIRTIO_INPUT
	TRACE_ALWAYS("%" B_PRIdSSIZE ": ", pkt - fPackets);
	WriteInputPacket(pkt->data);
	TRACE("\n");
#endif

	fReadyPackets[fReadyPacketWptr & (fPacketCnt - 1)] = pkt;
	fReadyPacketWptr++;

	fCanReadCond.NotifyOne();
}


status_t
PacketQueue::Read(Packet*& pkt)
{
	InterruptsSpinLocker lock(&fLock);

	while (fReadyPacketRptr == fReadyPacketWptr) {
		status_t res = fCanReadCond.Wait(&fLock, B_CAN_INTERRUPT);

		if (res < B_OK)
			return res;
	}

	pkt = fReadyPackets[fReadyPacketRptr & (fPacketCnt - 1)];
	fReadyPacketRptr++;

	return B_OK;
}


// #pragma mark - VirtioInputDevFsNodeHandle

void VirtioInputDevFsNodeHandle::Free()
{
	delete this;
}


status_t VirtioInputDevFsNodeHandle::Control(uint32 op, void* buffer, size_t length)
{
	CALLED();
	TRACE("control(op = %" B_PRIu32 ")\n", op);

	switch (op) {
		case virtioInputRead: {
			TRACE("virtioInputRead\n");
			if (buffer == NULL || length < sizeof(VirtioInputPacket))
				return B_BAD_VALUE;

			Packet* pkt;
			status_t res = fDriver.fPacketQueue.Read(pkt);
			if (res < B_OK)
				return res;

			res = user_memcpy(buffer, pkt, sizeof(VirtioInputPacket));

			physical_entry pe = fDriver.fPacketQueue.PacketPhysEntry(pkt);
			mutex_lock(&fDriver.fVirtioQueueLock);
			fDriver.fVirtioQueue->Request(NULL, &pe, pkt);
			mutex_unlock(&fDriver.fVirtioQueueLock);

			if (res < B_OK)
				return res;

			return B_OK;
		}
	}

	return B_DEV_INVALID_IOCTL;
}


// #pragma mark - VirtioInputDevFsNode

status_t
VirtioInputDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle** outHandle)
{
	ObjectDeleter<VirtioInputDevFsNodeHandle> handle(new(std::nothrow) VirtioInputDevFsNodeHandle(fDriver));
	if (!handle.IsSet())
		return B_NO_MEMORY;

	*outHandle = handle.Detach();
	return B_OK;
}


// #pragma mark - VirtioInputDriver

status_t
VirtioInputDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<VirtioInputDriver> driver(new(std::nothrow) VirtioInputDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


void
VirtioInputDriver::Free()
{
	delete this;
}


status_t
VirtioInputDriver::Init()
{
	CALLED();

	fVirtioDevice = fNode->QueryBusInterface<VirtioDevice>();

	fVirtioDevice->NegotiateFeatures(0, &fFeatures, NULL);

	fPacketQueue.Init(8);

	CHECK_RET(fVirtioDevice->AllocQueues(1, &fVirtioQueue));
	CHECK_RET(fVirtioQueue->SetupInterrupt(InterruptCallback, this));

	for (uint32 i = 0; i < fPacketQueue.PacketCount(); i++) {
		Packet* pkt = fPacketQueue.PacketAt(i);
		physical_entry pe = fPacketQueue.PacketPhysEntry(pkt);
		fVirtioQueue->Request(NULL, &pe, pkt);
	}

	return B_OK;
}


void
VirtioInputDriver::InterruptCallback(void* driverCookie, void* cookie)
{
	CALLED();
	VirtioInputDriver* drv = (VirtioInputDriver*)cookie;

	Packet* pkt;
	while (drv->fVirtioQueue->Dequeue((void**)&pkt, NULL))
		drv->fPacketQueue.Write(pkt);
}


status_t
VirtioInputDriver::RegisterChildDevices()
{
	CALLED();

	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), "input/virtio/%" B_PRId32 "/raw", id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}


static driver_module_info sVirtioInputModuleInfo = {
	.info = {
		.name = VIRTIO_INPUT_DRIVER_MODULE_NAME,
	},
	.probe = VirtioInputDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sVirtioInputModuleInfo,
	NULL
};
