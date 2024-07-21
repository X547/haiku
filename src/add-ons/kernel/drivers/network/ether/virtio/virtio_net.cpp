/*
 * Copyright 2013, 2018, Jérôme Duval, jerome.duval@gmail.com.
 * Copyright 2017, Philippe Houdoin, philippe.houdoin@gmail.com.
 * Distributed under the terms of the MIT License.
 */

#include "virtio_net.h"

#include <stdio.h>
#include <string.h>
#include <new>

#include <kernel.h>
#include <condition_variable.h>
#include <net/if_media.h>

#include <AutoDeleter.h>


#define VIRTIO_NET_DRIVER_MODULE_NAME "drivers/network/virtio_net/driver/v1"


#define ROUND_TO_PAGE_SIZE(x) (((x) + (B_PAGE_SIZE) - 1) & ~((B_PAGE_SIZE) - 1))


const char*
get_feature_name(uint64 feature)
{
	switch (feature) {
		case VIRTIO_NET_F_CSUM:
			return "host checksum";
		case VIRTIO_NET_F_GUEST_CSUM:
			return "guest checksum";
		case VIRTIO_NET_F_MTU:
			return "mtu";
		case VIRTIO_NET_F_MAC:
			return "macaddress";
		case VIRTIO_NET_F_GSO:
			return "host allgso";
		case VIRTIO_NET_F_GUEST_TSO4:
			return "guest tso4";
		case VIRTIO_NET_F_GUEST_TSO6:
			return "guest tso6";
		case VIRTIO_NET_F_GUEST_ECN:
			return "guest tso6+ecn";
		case VIRTIO_NET_F_GUEST_UFO:
			return "guest ufo";
		case VIRTIO_NET_F_HOST_TSO4:
			return "host tso4";
		case VIRTIO_NET_F_HOST_TSO6:
			return "host tso6";
		case VIRTIO_NET_F_HOST_ECN:
			return "host tso6+ecn";
		case VIRTIO_NET_F_HOST_UFO:
			return "host UFO";
		case VIRTIO_NET_F_MRG_RXBUF:
			return "host mergerxbuffers";
		case VIRTIO_NET_F_STATUS:
			return "status";
		case VIRTIO_NET_F_CTRL_VQ:
			return "control vq";
		case VIRTIO_NET_F_CTRL_RX:
			return "rx mode";
		case VIRTIO_NET_F_CTRL_VLAN:
			return "vlan filter";
		case VIRTIO_NET_F_CTRL_RX_EXTRA:
			return "rx mode extra";
		case VIRTIO_NET_F_GUEST_ANNOUNCE:
			return "guest announce";
		case VIRTIO_NET_F_MQ:
			return "multiqueue";
		case VIRTIO_NET_F_CTRL_MAC_ADDR:
			return "set macaddress";
	}
	return NULL;
}


status_t
VirtioNetDriver::DrainQueues()
{
	BufInfo* buf = NULL;
	while (fTxQueues[0]->Dequeue((void**)&buf, NULL))
		fTxFreeList.Add(buf);

	while (fRxQueues[0]->Dequeue(NULL, NULL))
		;

	while (fRxFullList.RemoveHead() != NULL)
		;

	return B_OK;
}


status_t
VirtioNetDriver::RxEnqueueBuf(BufInfo* buf)
{
	CALLED();
	physical_entry entries[2];
	entries[0] = buf->hdrEntry;
	entries[1] = buf->entry;

	memset(buf->hdr, 0, sizeof(struct virtio_net_hdr));

	// queue the rx buffer
	status_t status = fRxQueues[0]->RequestV(entries, 0, 2, buf);
	if (status != B_OK) {
		ERROR("rx queueing on queue %d failed (%s)\n", 0, strerror(status));
		return status;
	}

	return B_OK;
}


status_t
VirtioNetDriver::CtrlExecCmd(int cmd, int value)
{
	struct {
		struct virtio_net_ctrl_hdr hdr;
		uint8 pad1;
		uint8 onoff;
		uint8 pad2;
		uint8 ack;
	} s __attribute__((aligned(2)));

	s.hdr.net_class = VIRTIO_NET_CTRL_RX;
	s.hdr.cmd = cmd;
	s.onoff = value == 0;
	s.ack = VIRTIO_NET_ERR;

	physical_entry entries[3];
	status_t status = get_memory_map(&s.hdr, sizeof(s.hdr), &entries[0], 1);
	if (status != B_OK)
		return status;
	status = get_memory_map(&s.onoff, sizeof(s.onoff), &entries[1], 1);
	if (status != B_OK)
		return status;
	status = get_memory_map(&s.ack, sizeof(s.ack), &entries[2], 1);
	if (status != B_OK)
		return status;

	if (!fCtrlQueue->IsEmpty())
		return B_ERROR;

	status = fCtrlQueue->RequestV(entries, 2, 1, NULL);
	if (status != B_OK)
		return status;

	while (!fCtrlQueue->Dequeue(NULL, NULL))
		spin(10);

	return s.ack == VIRTIO_NET_OK ? B_OK : B_IO_ERROR;
}


status_t
VirtioNetDriver::SetPromisc(int value)
{
	return CtrlExecCmd(VIRTIO_NET_CTRL_RX_PROMISC, value);
}


int
VirtioNetDriver::SetAllmulti(int value)
{
	return CtrlExecCmd(VIRTIO_NET_CTRL_RX_ALLMULTI, value);
}


status_t
VirtioNetDriver::Init()
{
	CALLED();
	fDevice = fNode->QueryBusInterface<VirtioDevice>();

	fDevice->NegotiateFeatures(
		VIRTIO_NET_F_STATUS | VIRTIO_NET_F_MAC | VIRTIO_NET_F_MTU
		| VIRTIO_NET_F_CTRL_VQ | VIRTIO_NET_F_CTRL_RX
		/* | VIRTIO_NET_F_MQ */,
		 &fFeatures, &get_feature_name);

	if ((fFeatures & VIRTIO_NET_F_MQ) != 0
			&& (fFeatures & VIRTIO_NET_F_CTRL_VQ) != 0
			&& fDevice->ReadDeviceConfig(
				offsetof(struct virtio_net_config, max_virtqueue_pairs),
				&fPairsCount, sizeof(fPairsCount)) == B_OK) {
		system_info sysinfo;
		if (get_system_info(&sysinfo) == B_OK
			&& fPairsCount > sysinfo.cpu_count) {
			fPairsCount = sysinfo.cpu_count;
		}
	} else
		fPairsCount = 1;

	// TODO read config

	// Setup queues
	uint32 queueCount = fPairsCount * 2;
	if ((fFeatures & VIRTIO_NET_F_CTRL_VQ) != 0)
		queueCount++;
	VirtioQueue* virtioQueues[queueCount];
	status_t status = fDevice->AllocQueues(queueCount, virtioQueues);
	if (status != B_OK) {
		ERROR("queue allocation failed (%s)\n", strerror(status));
		return status;
	}

	char* rxBuffer;
	char* txBuffer;

	fRxQueues.SetTo(new(std::nothrow) VirtioQueue*[fPairsCount]);
	fTxQueues.SetTo(new(std::nothrow) VirtioQueue*[fPairsCount]);
	fRxSizes.SetTo(new(std::nothrow) uint16[fPairsCount]);
	fTxSizes.SetTo(new(std::nothrow) uint16[fPairsCount]);
	if (!fRxQueues.IsSet() || !fTxQueues.IsSet()
		|| !fRxSizes.IsSet() || !fTxSizes.IsSet()) {
		return B_NO_MEMORY;
	}
	for (uint32 i = 0; i < fPairsCount; i++) {
		fRxQueues[i] = virtioQueues[i * 2];
		fTxQueues[i] = virtioQueues[i * 2 + 1];
		fRxSizes[i] = fRxQueues[i]->Size() / 2;
		fTxSizes[i] = fTxQueues[i]->Size() / 2;
	}
	if ((fFeatures & VIRTIO_NET_F_CTRL_VQ) != 0)
		fCtrlQueue = virtioQueues[fPairsCount * 2];

	fRxBufInfos.SetTo(new(std::nothrow) BufInfo*[fRxSizes[0]]);
	fTxBufInfos.SetTo(new(std::nothrow) BufInfo*[fTxSizes[0]]);
	if (!fRxBufInfos.IsSet() || !fTxBufInfos.IsSet()) {
		return B_NO_MEMORY;
	}
	memset(&fRxBufInfos[0], 0, sizeof(BufInfo*) * fRxSizes[0]);
	memset(&fTxBufInfos[0], 0, sizeof(BufInfo*) * fTxSizes[0]);

	// create receive buffer area
	fRxArea.SetTo(create_area("virtionet rx buffer", (void**)&rxBuffer,
		B_ANY_KERNEL_BLOCK_ADDRESS, ROUND_TO_PAGE_SIZE(
			BUFFER_SIZE * fRxSizes[0]),
		B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));
	if (!fRxArea.IsSet()) {
		return fRxArea.Get();
	}

	// initialize receive buffer descriptors
	for (int i = 0; i < fRxSizes[0]; i++) {
		BufInfo* buf = new(std::nothrow) BufInfo;
		if (buf == NULL) {
			return B_NO_MEMORY;
		}

		fRxBufInfos[i] = buf;
		buf->hdr = (struct virtio_net_hdr*)((addr_t)rxBuffer
			+ i * BUFFER_SIZE);
		buf->buffer = (uint8*)((addr_t)buf->hdr + sizeof(virtio_net_rx_hdr));

		status = get_memory_map(buf->buffer,
			BUFFER_SIZE - sizeof(virtio_net_rx_hdr), &buf->entry, 1);
		if (status != B_OK)
			return status;

		status = get_memory_map(buf->hdr, sizeof(struct virtio_net_hdr),
			&buf->hdrEntry, 1);
		if (status != B_OK)
			return status;
	}

	// create transmit buffer area
	fTxArea.SetTo(create_area("virtionet tx buffer", (void**)&txBuffer,
		B_ANY_KERNEL_BLOCK_ADDRESS, ROUND_TO_PAGE_SIZE(BUFFER_SIZE * fTxSizes[0]),
		B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA));
	if (!fTxArea.IsSet()) {
		return fTxArea.Get();
	}

	// initialize transmit buffer descriptors
	for (int i = 0; i < fTxSizes[0]; i++) {
		BufInfo* buf = new(std::nothrow) BufInfo;
		if (buf == NULL) {
			return B_NO_MEMORY;
		}

		fTxBufInfos[i] = buf;
		buf->hdr = (struct virtio_net_hdr*)((addr_t)txBuffer
			+ i * BUFFER_SIZE);
		buf->buffer = (uint8*)((addr_t)buf->hdr + sizeof(virtio_net_tx_hdr));

		status = get_memory_map(buf->buffer,
			BUFFER_SIZE - sizeof(virtio_net_tx_hdr), &buf->entry, 1);
		if (status != B_OK)
			return status;

		status = get_memory_map(buf->hdr, sizeof(struct virtio_net_hdr),
			&buf->hdrEntry, 1);
		if (status != B_OK)
			return status;

		fTxFreeList.Add(buf);
	}

	// Setup interrupt
	status = fDevice->SetupInterrupt(NULL, this);
	if (status != B_OK) {
		ERROR("interrupt setup failed (%s)\n", strerror(status));
		return status;
	}

	status = fRxQueues[0]->SetupInterrupt(RxDone, this);
	if (status != B_OK) {
		ERROR("queue interrupt setup failed (%s)\n", strerror(status));
		return status;
	}

	status = fTxQueues[0]->SetupInterrupt(TxDone, this);
	if (status != B_OK) {
		ERROR("queue interrupt setup failed (%s)\n", strerror(status));
		return status;
	}

	if ((fFeatures & VIRTIO_NET_F_CTRL_VQ) != 0) {
		status = fCtrlQueue->SetupInterrupt(NULL, this);
		if (status != B_OK) {
			ERROR("queue interrupt setup failed (%s)\n", strerror(status));
			return status;
		}
	}

	static int32 lastId = 0;
	int32 id = lastId++;

	char name[64];
	snprintf(name, sizeof(name), "net/virtio/%" B_PRId32, id);

	CHECK_RET(fNode->RegisterDevFsNode(name, &fDevFsNode));

	return B_OK;
}

VirtioNetDriver::~VirtioNetDriver()
{
	CALLED();

	fDevice->FreeInterrupts();

	while (true) {
		BufInfo* buf = fTxFreeList.RemoveHead();
		if (buf == NULL)
			break;
	}

	for (int i = 0; i < fRxSizes[0]; i++) {
		delete fRxBufInfos[i];
	}
	for (int i = 0; i < fTxSizes[0]; i++) {
		delete fTxBufInfos[i];
	}

	fDevice->FreeQueues();
}




DevFsNode::Capabilities
VirtioNetDriver::DevFsNode::GetCapabilities() const
{
	return {.read = true, .write = true, .control = true};
}


status_t
VirtioNetDriver::DevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	CALLED();
	if (Base().fOpen)
		return B_BUSY;

	Base().fNonblocking = (openMode & O_NONBLOCK) != 0;
	Base().fMaxFrameSize = MAX_FRAME_SIZE;
	Base().fRxDone.SetTo(create_sem(0, "virtio_net_rx"));
	CHECK_RET(Base().fRxDone.Get());
	Base().fTxDone.SetTo(create_sem(1, "virtio_net_tx"));
	CHECK_RET(Base().fTxDone.Get());

	if ((Base().fFeatures & VIRTIO_NET_F_MAC) != 0) {
		Base().fDevice->ReadDeviceConfig(offsetof(struct virtio_net_config, mac),&Base().fMacAddr, sizeof(Base().fMacAddr));
	}

	if ((Base().fFeatures & VIRTIO_NET_F_MTU) != 0) {
		dprintf("mtu feature\n");
		uint16 mtu;
		Base().fDevice->ReadDeviceConfig(offsetof(struct virtio_net_config, mtu), &mtu, sizeof(mtu));
		// check against minimum MTU
		if (mtu > 68)
			Base().fMaxFrameSize = mtu;
		else
			Base().fDevice->ClearFeature(VIRTIO_NET_F_MTU);
	} else {
		dprintf("no mtu feature\n");
	}

	for (int i = 0; i < Base().fRxSizes[0]; i++)
		Base().RxEnqueueBuf(Base().fRxBufInfos[i]);

	Base().fOpen = true;
	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;
}


status_t
VirtioNetDriver::DevFsNode::Close()
{
	CALLED();
	Base().fOpen = false;

	Base().fRxDone.Unset();
	Base().fTxDone.Unset();

	Base().DrainQueues();

	return B_OK;
}


void
VirtioNetDriver::RxDone(void* driverCookie, void* cookie)
{
	CALLED();
	auto driver = (VirtioNetDriver*)cookie;

	release_sem_etc(driver->fRxDone.Get(), 1, B_DO_NOT_RESCHEDULE);
}


status_t
VirtioNetDriver::DevFsNode::Read(off_t pos, void* buffer, size_t* _length)
{
	CALLED();

	mutex_lock(&Base().fRxLock);
	while (Base().fRxFullList.Head() == NULL) {
		mutex_unlock(&Base().fRxLock);

		if (Base().fNonblocking)
			return B_WOULD_BLOCK;
		TRACE("virtio_net_read: waiting\n");
		status_t status = acquire_sem(Base().fRxDone.Get());
		if (status != B_OK) {
			ERROR("acquire_sem(rxDone) failed (%s)\n", strerror(status));
			return status;
		}
		int32 semCount = 0;
		get_sem_count(Base().fRxDone.Get(), &semCount);
		if (semCount > 0)
			acquire_sem_etc(Base().fRxDone.Get(), semCount, B_RELATIVE_TIMEOUT, 0);

		mutex_lock(&Base().fRxLock);
		while (Base().fRxDone.IsSet()) {
			uint32 usedLength = 0;
			BufInfo* buf = NULL;
			if (!Base().fRxQueues[0]->Dequeue((void**)&buf, &usedLength) || buf == NULL) {
				break;
			}

			buf->rxUsedLength = usedLength;
			Base().fRxFullList.Add(buf);
		}
		TRACE("virtio_net_read: finished waiting\n");
	}

	BufInfo* buf = Base().fRxFullList.RemoveHead();
	*_length = MIN(buf->rxUsedLength, *_length);
	memcpy(buffer, buf->buffer, *_length);
	Base().RxEnqueueBuf(buf);
	mutex_unlock(&Base().fRxLock);
	return B_OK;
}


void
VirtioNetDriver::TxDone(void* driverCookie, void* cookie)
{
	CALLED();
	auto driver = (VirtioNetDriver*)cookie;

	release_sem_etc(driver->fTxDone.Get(), 1, B_DO_NOT_RESCHEDULE);
}


status_t
VirtioNetDriver::DevFsNode::Write(off_t pos, const void* buffer, size_t* _length)
{
	CALLED();

	mutex_lock(&Base().fTxLock);
	while (Base().fTxFreeList.Head() == NULL) {
		mutex_unlock(&Base().fTxLock);
		if (Base().fNonblocking)
			return B_WOULD_BLOCK;

		status_t status = acquire_sem(Base().fTxDone.Get());
		if (status != B_OK) {
			ERROR("acquire_sem(txDone) failed (%s)\n", strerror(status));
			return status;
		}

		int32 semCount = 0;
		get_sem_count(Base().fTxDone.Get(), &semCount);
		if (semCount > 0)
			acquire_sem_etc(Base().fTxDone.Get(), semCount, B_RELATIVE_TIMEOUT, 0);

		mutex_lock(&Base().fTxLock);
		while (Base().fTxDone.IsSet()) {
			BufInfo* buf = NULL;
			if (!Base().fTxQueues[0]->Dequeue((void**)&buf, NULL) || buf == NULL) {
				break;
			}

			Base().fTxFreeList.Add(buf);
		}
	}
	BufInfo* buf = Base().fTxFreeList.RemoveHead();

	TRACE("virtio_net_write: copying %lu\n", MIN(MAX_FRAME_SIZE, *_length));
	memcpy(buf->buffer, buffer, MIN(MAX_FRAME_SIZE, *_length));
	memset(buf->hdr, 0, sizeof(virtio_net_hdr));

	physical_entry entries[2];
	entries[0] = buf->hdrEntry;
	entries[0].size = sizeof(virtio_net_hdr);
	entries[1] = buf->entry;
	entries[1].size = MIN(MAX_FRAME_SIZE, *_length);

	// queue the virtio_net_hdr + buffer data
	status_t status = Base().fTxQueues[0]->RequestV(entries, 2, 0, buf);
	mutex_unlock(&Base().fTxLock);
	if (status != B_OK) {
		ERROR("tx queueing on queue %d failed (%s)\n", 0, strerror(status));
		return status;
	}

	return B_OK;
}


status_t
VirtioNetDriver::DevFsNode::Control(uint32 op, void *buffer, size_t length, bool isKernel)
{
	// CALLED();

	// TRACE("ioctl(op = %lx)\n", op);

	switch (op) {
		case ETHER_GETADDR:
			TRACE("ioctl: get macaddr\n");
			return user_memcpy(buffer, &Base().fMacAddr, sizeof(Base().fMacAddr));

		case ETHER_INIT:
			TRACE("ioctl: init\n");
			return B_OK;

		case ETHER_GETFRAMESIZE:
			TRACE("ioctl: get frame size\n");
			if (length != sizeof(Base().fMaxFrameSize))
				return B_BAD_VALUE;

			return user_memcpy(buffer, &Base().fMaxFrameSize, sizeof(Base().fMaxFrameSize));

		case ETHER_SETPROMISC:
		{
			TRACE("ioctl: set promisc\n");
			int32 value;
			if (length != sizeof(value))
				return B_BAD_VALUE;
			if (user_memcpy(&value, buffer, sizeof(value)) != B_OK)
				return B_BAD_ADDRESS;
			if (Base().fPromiscuous == value)
				return B_OK;
			Base().fPromiscuous = value;
			return Base().SetPromisc(value);
		}
		case ETHER_NONBLOCK:
		{
			TRACE("ioctl: non blocking ? %s\n", Base().fNonblocking ? "yes" : "no");
			int32 value;
			if (length != sizeof(value))
				return B_BAD_VALUE;
			if (user_memcpy(&value, buffer, sizeof(value)) != B_OK)
				return B_BAD_ADDRESS;
			Base().fNonblocking = value == 0;
			return B_OK;
		}
		case ETHER_ADDMULTI:
		{
			uint32 i, multiCount = Base().fMultiCount;
			TRACE("ioctl: add multicast\n");

			if ((Base().fFeatures & VIRTIO_NET_F_CTRL_RX) == 0)
				return B_NOT_SUPPORTED;

			if (multiCount == B_COUNT_OF(Base().fMulti))
				return B_ERROR;

			for (i = 0; i < multiCount; i++) {
				if (memcmp(&Base().fMulti[i], buffer,
					sizeof(Base().fMulti[0])) == 0) {
					break;
				}
			}

			if (i == multiCount) {
				memcpy(&Base().fMulti[i], buffer, sizeof(Base().fMulti[i]));
				Base().fMultiCount++;
			}
			if (Base().fMultiCount == 1) {
				TRACE("Enabling multicast\n");
				Base().SetAllmulti(1);
			}

			return B_OK;
		}
		case ETHER_REMMULTI:
		{
			uint32 i, multiCount = Base().fMultiCount;
			TRACE("ioctl: remove multicast\n");

			if ((Base().fFeatures & VIRTIO_NET_F_CTRL_RX) == 0)
				return B_NOT_SUPPORTED;

			for (i = 0; i < multiCount; i++) {
				if (memcmp(&Base().fMulti[i], buffer, sizeof(Base().fMulti[0])) == 0) {
					break;
				}
			}

			if (i != multiCount) {
				if (i < multiCount - 1) {
					memmove(&Base().fMulti[i], &Base().fMulti[i + 1],
						sizeof(Base().fMulti[i]) * (multiCount - i - 1));
				}
				Base().fMultiCount--;
				if (Base().fMultiCount == 0) {
					TRACE("Disabling multicast\n");
					Base().SetAllmulti(0);
				}
				return B_OK;
			}
			return B_BAD_VALUE;
		}
		case ETHER_GET_LINK_STATE:
		{
			TRACE("ioctl: get link state\n");
			ether_link_state_t state;
			uint16 status = VIRTIO_NET_S_LINK_UP;
			if ((Base().fFeatures & VIRTIO_NET_F_STATUS) != 0) {
				Base().fDevice->ReadDeviceConfig(
					offsetof(struct virtio_net_config, status),
					&status, sizeof(status));
			}
			state.media = ((status & VIRTIO_NET_S_LINK_UP) != 0 ? IFM_ACTIVE : 0)
				| IFM_ETHER | IFM_FULL_DUPLEX | IFM_10G_T;
			state.speed = 10000000000ULL;
			state.quality = 1000;

			return user_memcpy(buffer, &state, sizeof(ether_link_state_t));
		}

		default:
			ERROR("ioctl: unknown message %" B_PRIx32 "\n", op);
			break;
	}

	return B_DEV_INVALID_IOCTL;
}


status_t
VirtioNetDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<VirtioNetDriver> driver(new(std::nothrow) VirtioNetDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


static driver_module_info sVirtioNetDriver = {
	.info = {
		.name = VIRTIO_NET_DRIVER_MODULE_NAME,
	},
	.probe = VirtioNetDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sVirtioNetDriver,
	NULL
};
