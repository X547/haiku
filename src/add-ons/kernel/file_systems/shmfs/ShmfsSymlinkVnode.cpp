#include "Shmfs.h"

#include <KernelExport.h>
#include <NodeMonitor.h>

#include <util/AutoLock.h>

#include <new>
#include <algorithm>


status_t ShmfsSymlinkVnode::SetPath(const char *path)
{
	size_t len = strlen(path) + 1;
	ArrayDeleter<char> newPath(new(std::nothrow) char[len]);
	if (!newPath.IsSet())
		return B_NO_MEMORY;
	memcpy(&newPath[0], path, len);
	fPath.SetTo(newPath.Detach());
	return B_OK;
}


//#pragma mark - VFS interface

status_t ShmfsSymlinkVnode::ReadStat(struct stat &stat)
{
	RecursiveLocker lock(Volume()->Lock());
	CHECK_RET(ShmfsVnode::ReadStat(stat));
	stat.st_mode |= S_IFLNK;
	stat.st_size = strlen(GetPath());
	return B_OK;
}

status_t ShmfsSymlinkVnode::ReadSymlink(char* buffer, size_t &bufferSize)
{
	RecursiveLocker lock(Volume()->Lock());
	const char* path = GetPath();
	size_t len = strlen(path);
	memcpy(buffer, path, std::min<size_t>(len, bufferSize));
	bufferSize = len;
	return B_OK;
}
