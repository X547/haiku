#include "Shmfs.h"

#include <sys/stat.h>
#include <NodeMonitor.h>

#include <algorithm>


status_t ShmfsAttribute::EnsureSize(uint32 size)
{
	if (size > fDataAllocSize) {
		uint32 newSize = size + size/2;
		ArrayDeleter<uint8> newData(new(std::nothrow) uint8[newSize]);
		if (!newData.IsSet())
			return B_NO_MEMORY;
		memcpy(&newData[0], &fData[0], fDataSize);
		fData.SetTo(newData.Detach());
		fDataAllocSize = newSize;
	}
	return B_OK;
}


status_t ShmfsAttribute::SetName(const char* name)
{
	size_t len = strlen(name) + 1;
	ArrayDeleter<char> newName(new(std::nothrow) char[len]);
	if (!newName.IsSet())
		return B_NO_MEMORY;
	memcpy(&newName[0], name, len);
	fName.SetTo(newName.Detach());
	return B_OK;
}


status_t ShmfsAttribute::Read(off_t pos, void* buffer, size_t &length)
{
	if (pos < 0)
		return B_BAD_VALUE;
	pos = std::min<off_t>(pos, fDataSize);
	length = std::min<size_t>(length, size_t(fDataSize - pos));
	memcpy(buffer, &fData[pos], length);
	return B_OK;
}

status_t ShmfsAttribute::Write(off_t pos, const void* buffer, size_t &length)
{
	if (pos < 0)
		return B_BAD_VALUE;
	off_t newSize = pos + length;
	if (newSize <= 0) {
		length = 0;
		return B_OK;
	}
	if (newSize > fDataSize)
		CHECK_RET(WriteStat({.st_size = newSize}, B_STAT_SIZE));
	memcpy(&fData[pos], buffer, length);
	return B_OK;
}

status_t ShmfsAttribute::ReadStat(struct stat &stat)
{
	stat.st_mode = S_ATTR;
	stat.st_size = fDataSize;
	stat.st_type = fType;
	return B_OK;
}

status_t ShmfsAttribute::WriteStat(const struct stat &stat, int statMask)
{
	if ((statMask & B_STAT_SIZE) != 0) {
		CHECK_RET(EnsureSize(stat.st_size));
		if (stat.st_size > fDataSize)
			memset(&fData[fDataSize], 0, stat.st_size - fDataSize);
		fDataSize = stat.st_size;
	}
	return B_OK;
}
