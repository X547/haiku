#ifndef _VIRTIO_H_
#define _VIRTIO_H_


#include <SupportDefs.h>


enum {
	virtioRegsSize = 0x1000,
	virtioSignature = 0x74726976,
	virtioVendorId = 0x554d4551,
};

enum {
	virtioDevNet = 1,
	virtioDevBlock = 2,
	virtioDevConsole = 3,
	virtioDev9p = 9,
	virtioDevInput = 18,
};

enum {
	virtioConfigSAcknowledge = 1 << 0,
	virtioConfigSDriver      = 1 << 1,
	virtioConfigSDriverOk    = 1 << 2,
	virtioConfigSFeaturesOk  = 1 << 3,
};

enum {
	vringDescFlagsNext     = 1 << 0,
	vringDescFlagsWrite    = 1 << 1,
	vringDescFlagsIndirect = 1 << 2,
};

struct VirtioRegs {
	uint32 signature;
	uint32 version;
	uint32 deviceId;
	uint32 vendorId;
	uint32 deviceFeatures;
	uint32 unknown1[3];
	uint32 driverFeatures;
	uint32 unknown2[1];
	uint32 guestPageSize; /* version 1 only */
	uint32 unknown3[1];
	uint32 queueSel;
	uint32 queueNumMax;
	uint32 queueNum;
	uint32 queueAlign;    /* version 1 only */
	uint32 queuePfn;      /* version 1 only */
	uint32 queueReady;
	uint32 unknown4[2];
	uint32 queueNotify;
	uint32 unknown5[3];
	uint32 interruptStatus;
	uint32 interruptAck;
	uint32 unknown6[2];
	uint32 status;
	uint32 unknown7[3];
	uint32 queueDescLow;
	uint32 queueDescHi;
	uint32 unknown8[2];
	uint32 queueAvailLow;
	uint32 queueAvailHi;
	uint32 unknown9[2];
	uint32 queueUsedLow;
	uint32 queueUsedHi;
	uint32 unknown10[21];
	uint32 configGeneration;
	uint8 config[3840];
};

struct VirtioDesc {
	uint64 addr;
	uint32 len;
	uint16 flags;
	uint16 next;
};
// filled by driver
struct VirtioAvail {
	uint16 flags;
	uint16 idx;
	uint16 ring[0];
};
struct VirtioUsedItem
{
	uint32 id;
	uint32 len;
};
// filled by device
struct VirtioUsed {
	uint16 flags;
	uint16 idx;
	VirtioUsedItem ring[0];
};


// Input

enum {
	virtioInputCfgUnset    = 0,
	virtioInputCfgIdName   = 1,
	virtioInputCfgIdSerial = 2,
	virtioInputCfgIdDevids = 3,
	virtioInputCfgPropBits = 0x10,
	virtioInputCfgEvBits   = 0x11,
	virtioInputCfgAbsInfo  = 0x12,
};

enum {
	virtioInputEvSyn = 0,
	virtioInputEvKey = 1,
	virtioInputEvRel = 2,
	virtioInputEvAbs = 3,
	virtioInputEvRep = 4,
};

enum {
	btnLeft     = 0x110,
	btnRight    = 0x111,
	btnMiddle   = 0x112,
	btnGearDown = 0x150,
	btnGearUp   = 0x151,
};

enum {
	relX     = 0,
	relY     = 1,
	relZ     = 2,
	relWheel = 8,
};

enum {
	absX = 0,
	absY = 1,
	absZ = 2,
};

struct VirtioInputPacket {
	uint16 type;
	uint16 code;
	int32 value;
};


enum {
	virtioBlockTypeIn       = 0,
	virtioBlockTypeOut      = 1,
	virtioBlockTypeFlush    = 4,
	virtioBlockTypeFlushOut = 5,
};

enum {
	virtioBlockStatusOk          = 0,
	virtioBlockStatusIoError     = 1,
	virtioBlockStatusUnsupported = 2,
};

enum {
	virtioBlockSectorSize = 512,
};

struct VirtioBlockRequest {
	uint32 type;
	uint32 ioprio;
	uint64 sectorNum;
};


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

struct IORequest {
	IOState state;
	IOOperation op;
	void *buf;
	size_t len;
	IORequest *next;
	
	IORequest(IOOperation op, void *buf, size_t len): state(ioStateInactive), op(op), buf(buf), len(len), next(NULL) {}
};

class VirtioDevice {
private:
	VirtioRegs *volatile fRegs;
	size_t fQueueLen;
	VirtioDesc *volatile fDescs;
	VirtioAvail *volatile fAvail;
	VirtioUsed *volatile fUsed;
	uint32_t *fFreeDescs;
	uint32_t fLastUsed;
	IORequest **fReqs;

	int32_t AllocDesc();
	void FreeDesc(int32_t idx);

public:
	VirtioDevice(VirtioRegs *volatile regs);
	inline VirtioRegs *volatile Regs() {return fRegs;}
	void ScheduleIO(IORequest **reqs, uint32 cnt);
	void ScheduleIO(IORequest *req);
	IORequest *WaitIO();
};


extern uint32 *gVirtioBase;


void virtio_init();

VirtioRegs *volatile ThisVirtioDev(uint32 deviceId, int n);

int virtio_input_wait_for_key();


#endif	// _VIRTIO_H_
