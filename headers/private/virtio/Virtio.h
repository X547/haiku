#ifndef _VIRTIO_2_H_
#define _VIRTIO_2_H_

#include <SupportDefs.h>

enum {
	virtioRegsSize = 0x1000,
	virtioSignature = 0x74726976,
	virtioVendorId = 0x554d4551,
};

enum {
	virtioDevNet     =  1,
	virtioDevBlock   =  2,
	virtioDevConsole =  3,
	virtioDev9p      =  9,
	virtioDevInput   = 18,
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
	virtioInputBtnLeft     = 0x110,
	virtioInputBtnRight    = 0x111,
	virtioInputBtnMiddle   = 0x112,
	virtioInputBtnGearDown = 0x150,
	virtioInputBtnGearUp   = 0x151,
};

enum {
	virtioInputRelX     = 0,
	virtioInputRelY     = 1,
	virtioInputRelZ     = 2,
	virtioInputRelWheel = 8,
};

enum {
	virtioInputAbsX = 0,
	virtioInputAbsY = 1,
	virtioInputAbsZ = 2,
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


#endif	// _VIRTIO_2_H_
