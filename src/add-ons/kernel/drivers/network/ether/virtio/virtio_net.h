/*
 * Copyright 2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#pragma once

#include <dm2/bus/Virtio.h>

#include <condition_variable.h>
#include <lock.h>
#include <net/ether_driver.h>

#include <AutoDeleterOS.h>
#include <ContainerOf.h>

#include "virtio_net_defs.h"


//#define TRACE_VIRTIO_NET
#ifdef TRACE_VIRTIO_NET
#	define TRACE(x...) dprintf("virtio_net: " x)
#else
#	define TRACE(x...) ;
#endif
#define TRACE_ALWAYS(x...)	dprintf("virtio_net: " x)
#define ERROR(x...)			dprintf("\33[33mvirtio_net:\33[0m " x)
#define CALLED() 			TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define BUFFER_SIZE	2048
#define MAX_FRAME_SIZE 1536


struct virtio_net_rx_hdr {
	struct virtio_net_hdr	hdr;
	uint8					pad[4];
} _PACKED;


struct virtio_net_tx_hdr {
	union {
		struct virtio_net_hdr			hdr;
		struct virtio_net_hdr_mrg_rxbuf mhdr;
	};
} _PACKED;


class VirtioNetDriver: public DeviceDriver {
public:
	VirtioNetDriver(DeviceNode* node): fNode(node) {}
	virtual ~VirtioNetDriver();

	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	struct BufInfo {
		DoublyLinkedListLink<BufInfo> link;
		typedef DoublyLinkedList<
			BufInfo,
			DoublyLinkedListMemberGetLink<BufInfo, &BufInfo::link>
		> List;

		uint8* buffer;
		struct virtio_net_hdr* hdr;
		physical_entry entry;
		physical_entry hdrEntry;
		uint32 rxUsedLength;
	};

private:
	status_t DrainQueues();
	status_t RxEnqueueBuf(BufInfo* buf);
	status_t CtrlExecCmd(int cmd, int value);
	status_t SetPromisc(int value);
	int SetAllmulti(int value);

	status_t Init();

	static void RxDone(void* driverCookie, void* cookie);
	static void TxDone(void* driverCookie, void* cookie);

private:
	DeviceNode*		fNode;
	VirtioDevice*	fDevice {};

	bool			fOpen {};

	uint64			fFeatures {};

	uint32			fPairsCount {};

	ArrayDeleter<VirtioQueue*>	fRxQueues;
	ArrayDeleter<uint16>	fRxSizes;

	ArrayDeleter<BufInfo*>	fRxBufInfos;
	SemDeleter		fRxDone;
	AreaDeleter		fRxArea;
	BufInfo::List	fRxFullList;
	mutex			fRxLock = MUTEX_INITIALIZER("virtionet rx lock");

	ArrayDeleter<VirtioQueue*>	fTxQueues;
	ArrayDeleter<uint16>	fTxSizes;

	ArrayDeleter<BufInfo*>	fTxBufInfos;
	SemDeleter		fTxDone;
	AreaDeleter		fTxArea;
	BufInfo::List	fTxFreeList;
	mutex			fTxLock = MUTEX_INITIALIZER("virtionet tx lock");

	VirtioQueue*	fCtrlQueue {};

	bool			fNonblocking {};
	bool			fPromiscuous {};
	uint32			fMaxFrameSize {};
	ether_address_t	fMacAddr {};

	uint32			fMultiCount {};
	ether_address_t	fMulti[128] {};

	class DevFsNode: public ::DevFsNode, public ::DevFsNodeHandle {
	public:
		VirtioNetDriver &Base() {return ContainerOf(*this, &VirtioNetDriver::fDevFsNode);}

		Capabilities GetCapabilities() const final;
		status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;

		status_t Close() final;
		status_t Read(off_t pos, void* buffer, size_t* length) final;
		status_t Write(off_t pos, const void* buffer, size_t* length) final;
		status_t Control(uint32 op, void *buffer, size_t length, bool isKernel) final;
	} fDevFsNode;
};
