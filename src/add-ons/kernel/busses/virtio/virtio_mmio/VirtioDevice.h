#ifndef _VIRTIODEVICE_H_
#define _VIRTIODEVICE_H_

#include <Virtio.h>
#include <virtio.h>
#include <AutoDeleter.h>
#include <AutoDeleterOS.h>
#include <util/AutoLock.h>


enum IOState {
	ioStateInactive,
	ioStatePending,
	ioStateDone,
	ioStateFailed,
};

enum IOOperation {
	ioOpRead,
	ioOpWrite,
};

struct VirtioRequest {
	IOState state;
	IOOperation op;
	void *buf;
	size_t len;
	VirtioRequest *next;

	VirtioRequest(IOOperation op, void *buf, size_t len): state(ioStateInactive), op(op), buf(buf), len(len), next(NULL) {}
};

struct VirtioDevice;

struct VirtioQueue {
	VirtioDevice *fDev;
	int32_t fId;
	size_t fQueueLen;
	AreaDeleter fArea;
	VirtioDesc *volatile fDescs;
	VirtioAvail *volatile fAvail;
	VirtioUsed *volatile fUsed;
	ArrayDeleter<uint32_t> fFreeDescs;
	uint32_t fLastUsed;
	ArrayDeleter<VirtioRequest*> fReqs;

	int32_t AllocDesc();
	void FreeDesc(int32_t idx);

	VirtioQueue(VirtioDevice *dev, int32_t id);
	~VirtioQueue();
};

struct VirtioDevice
{
	mutex fLock;
	AreaDeleter fRegsArea;
	VirtioRegs *volatile fRegs;
	int32_t fIrq;
	int32_t fQueueCnt;
	VirtioQueue **fQueues;
	
	virtio_intr_func fConfigHandler;
	void *fConfigHandlerCookie;
	virtio_callback_func fQueueHandler;
	void *fQueueHandlerCookie;

	VirtioDevice();
	void Init(phys_addr_t regs, size_t regsLen, int32 irq, int32_t queueCnt);
	void ScheduleIO(int32_t queueId, VirtioRequest **reqs, uint32_t cnt);
	void ScheduleIO(int32_t queueId, VirtioRequest *req);
	VirtioRequest *ConsumeIO(int32_t queueId);
};


#endif	// _VIRTIODEVICE_H_
