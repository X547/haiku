/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _VIRTIOBLOCKDEVICE_H_
#define _VIRTIOBLOCKDEVICE_H_


#include "virtio.h"

#include <boot/partitions.h>

#include <AutoDeleter.h>


class VirtioBlockDevice : public Node {
public:
	VirtioBlockDevice(VirtioDevice* blockIo);
	virtual ~VirtioBlockDevice();

	virtual ssize_t ReadAt(void* cookie, off_t pos, void* buffer,
		size_t bufferSize);
	virtual ssize_t WriteAt(void* cookie, off_t pos, const void* buffer,
		size_t bufferSize) { return B_UNSUPPORTED; }
	virtual off_t Size() const {
		return (*(uint32*)(&fBlockIo->Regs()->config[0])
			+ ((uint64)(*(uint32*)(&fBlockIo->Regs()->config[4])) << 32)
			)*kVirtioBlockSectorSize;
	}

	uint32 BlockSize() const { return kVirtioBlockSectorSize; }
	bool ReadOnly() const { return false; }
private:
	ObjectDeleter<VirtioDevice> fBlockIo;
};


VirtioBlockDevice* CreateVirtioBlockDev(int id);


#endif	// _VIRTIOBLOCKDEVICE_H_
