/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#ifndef _ATABLOCKDEVICE_H_
#define _ATABLOCKDEVICE_H_


#include <boot/partitions.h>


class AtaBlockDevice : public Node {
public:
	AtaBlockDevice();
	virtual ~AtaBlockDevice();

	virtual ssize_t ReadAt(void* cookie, off_t pos, void* buffer,
		size_t bufferSize);
	virtual ssize_t WriteAt(void* cookie, off_t pos, const void* buffer,
		size_t bufferSize) { return B_UNSUPPORTED; }
	virtual off_t Size() const {
		return fSize;
	}

	uint32 BlockSize() const { return 512; }
	bool ReadOnly() const { return false; }

private:
	off_t fSize;	
};


AtaBlockDevice* CreateAtaBlockDev();


#endif	// _ATABLOCKDEVICE_H_
