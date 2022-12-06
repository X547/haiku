/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _NVMEBLOCKDEVICE_H_
#define _NVMEBLOCKDEVICE_H_

#include <boot/partitions.h>

#include <AutoDeleter.h>

void* aligned_malloc(size_t required_bytes, size_t alignment);
void  aligned_free(void* p);

struct NvmeRegs {
	uint32 cap1;
	uint32 cap2;
	uint32 version;
	uint32 intMaskSet;
	uint32 intMaskClear;
	uint32 ctrlConfig;
	uint32 unknown1;
	uint32 ctrlStatus;
	uint32 unknown2;
	union AdminQueueAttrs {
		struct {
			uint16 submQueueLen;
			uint16 complQueueLen;
		};
		uint32 val;
	} adminQueueAttrs;
	uint32 adminSubmQueueAdrLo;
	uint32 adminSubmQueueAdrHi;
	uint32 adminComplQueueAdrLo;
	uint32 adminComplQueueAdrHi;
	uint32 unknown32[1010];
	uint32 doorbell[18];
};

enum {
	nvmeIdentityNamespace = 0,
	nvmeIdentifyController = 1,
	nvmeIdentityNamespaceList = 2,
	nvmeIdentityNamespaceDescs = 3,
};

enum {
	nvmeAdminOpDeleteSubmQueue = 0,
	nvmeAdminOpCreateSubmQueue = 1,
	nvmeAdminOpDeleteComplQueue = 4,
	nvmeAdminOpCreateComplQueue = 5,
	nvmeAdminOpIdentity = 6,
	nvmeAdminOpAbort = 8,
	nvmeAdminOpGetFeatures = 9,
	nvmeAdminOpSetFeatures = 10,
};

enum {
	nvmeOpFlush = 0,
	nvmeOpWrite = 1,
	nvmeOpRead = 2,
	nvmeOpWriteZeroes = 8,
	nvmeOpDatasetMgmt = 9,
};

enum {
	nvmeStatusSuccess = 0,
	nvmeStatusBadOp = 1,
	// TODO
};

struct NvmeSubmissionPacket {
	uint8 opcode;
	uint8 flags;
	uint16 cmdId;
	uint8 unknown2[20];
	uint64 prp1;
	uint64 prp2;
	uint64 arg1;
	uint16 size;
	uint8 unknown4[14];
};

struct NvmeCompletionPacket {
	uint32 specific;
	uint32 reserved;
	uint16 submQueueHead;
	uint16 submQueueId;
	uint16 cmdId;
	union Status {
		struct {
			uint16 phase: 1;
			uint16 status: 15;
		};
		uint16 val;
	} status;
};

class NvmeBlockDevice : public Node {
public:
	NvmeBlockDevice(void* regs);
	virtual ~NvmeBlockDevice();
	status_t Init();

	ssize_t ReadAt(void* cookie, off_t pos, void* buffer, size_t bufferSize) final;
	ssize_t WriteAt(void* cookie, off_t pos, const void* buffer, size_t bufferSize) final;
	off_t Size() const final;

	uint32 BlockSize() const { return 512; }
	bool ReadOnly() const { return false; }

private:
	struct Queue {
		static void FreeSubm(NvmeSubmissionPacket* p) { aligned_free(p); }
		static void FreeCompl(NvmeCompletionPacket* p) { aligned_free(p); }

		CObjectDeleter<NvmeSubmissionPacket, void, FreeSubm> submArray;
		CObjectDeleter<NvmeCompletionPacket, void, FreeCompl> complArray;
		uint16 submLen{};
		uint16 complLen{};
		uint32 submHead{}, submTail{}, submPendingTail{};
		uint32 complHead{};
		bool   phase{};

		status_t Init();
	};

	enum {
		queueIdAdmin = 0,
		queueIdIo = 1,
	};

	volatile NvmeRegs* fRegs{};
	off_t fSize{};
	Queue fQueues[2];

	NvmeSubmissionPacket* BeginSubmission(uint32 queueId);
	void CommitSubmissions(uint32 queueId);
	uint16 CompletionStatus(uint32 queueId);
};


NvmeBlockDevice* CreateNvmeBlockDev(void* regsAdr);


#endif	// _NVMEBLOCKDEVICE_H_
