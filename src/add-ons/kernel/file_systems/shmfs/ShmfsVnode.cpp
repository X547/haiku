#include "Shmfs.h"

#include <NodeMonitor.h>
#include <dirent.h>

#include <util/AutoLock.h>

#include <new>


void GetCurrentTime(struct timespec &outTime)
{
	bigtime_t now = real_time_clock_usecs();
	outTime.tv_sec = now / 1000000LL;
	outTime.tv_nsec = (now % 1000000LL) * 1000;
}


//#pragma mark - ShmfsVnode

ShmfsVnode::~ShmfsVnode()
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("-ShmfsVnode(%" B_PRId64 ", \"%s\")\n", fId, Name());
	for (;;) {
		ShmfsAttribute *attr = fAttrs.LeftMost();
		if (attr == NULL)
			break;
		fAttrs.Remove(attr);
		attr->ReleaseReference();
	}
	if (fId != 0) {
		fVolume->fIds.Remove(this);
		fVolume->fIdPool.Free(fId);
	}
}

status_t ShmfsVnode::SetName(const char *name)
{
	size_t len = strlen(name) + 1;
	ArrayDeleter<char> newName(new(std::nothrow) char[len]);
	if (!newName.IsSet())
		return B_NO_MEMORY;
	memcpy(&newName[0], name, len);
	fName.SetTo(newName.Detach());
	return B_OK;
}


void ShmfsVnode::AttrIteratorRewind(ShmfsAttrDirIterator* cookie)
{
	cookie->attr = fAttrs.LeftMost();
}

bool ShmfsVnode::AttrIteratorGet(ShmfsAttrDirIterator* cookie, const char *&name, ShmfsAttribute *&attr)
{
		if (cookie->attr == NULL)
			return false;
		attr = cookie->attr;
		name = attr->Name();
		return true;
}

void ShmfsVnode::AttrIteratorNext(ShmfsAttrDirIterator* cookie)
{
		cookie->attr = fAttrs.Next(cookie->attr);
}

void ShmfsVnode::RemoveAttr(ShmfsAttribute *attr)
{
	for (ShmfsAttrDirIterator *it = fAttrIterators.First(); it != NULL; it = fAttrIterators.GetNext(it)) {
		if (it->attr == attr)
			AttrIteratorNext(it);
	}
	fAttrs.Remove(attr);
}


//#pragma mark - VFS interface

status_t ShmfsVnode::Lookup(const char* name, ino_t &id)
{
	TRACE("ShmfsVnode::Lookup()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::GetVnodeName(char* buffer, size_t bufferSize)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("ShmfsVnode::GetVnodeName()\n");
	strlcpy(buffer, Name(), bufferSize);
	return B_OK;
}

status_t ShmfsVnode::PutVnode(bool reenter)
{
	TRACE("ShmfsVnode::PutVnode()\n");
	ReleaseReference();
	return B_OK;
}

status_t ShmfsVnode::RemoveVnode(bool reenter)
{
	TRACE("ShmfsVnode::RemoveVnode()\n");
	ReleaseReference();
	return B_OK;
}

status_t ShmfsVnode::Ioctl(ShmfsFileCookie* cookie, uint32 op, void* buffer, size_t length)
{
	return B_DEV_INVALID_IOCTL;
}

status_t ShmfsVnode::SetFlags(ShmfsFileCookie* cookie, int flags)
{
	return ENOSYS;
}

status_t ShmfsVnode::Fsync()
{
	return B_OK;
}

status_t ShmfsVnode::ReadSymlink(char* buffer, size_t &bufferSize)
{
	TRACE("ShmfsVnode::ReadSymlink(%p, %p)\n", buffer, &bufferSize);
	return ENOSYS;
}

status_t ShmfsVnode::CreateSymlink(const char* name, const char* path, int mode)
{
	TRACE("ShmfsVnode::CreateSymlink(\"%s\", \"%s\")\n", name, path);
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::Unlink(const char* name)
{
	TRACE("ShmfsVnode::Unlink(\"%s\")\n", name);
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::Rename(const char* fromName, ShmfsVnode* toDir, const char* toName)
{
	TRACE("ShmfsVnode::Rename()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::Access(int mode)
{
	TRACE("ShmfsVnode::Access()\n");
	return B_OK;
}

status_t ShmfsVnode::ReadStat(struct stat &stat)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("ShmfsVnode::ReadStat()\n");

	stat = {
		.st_ino = fId,
		.st_mode = fMode,
		.st_nlink = 1,
		.st_uid = fUid,
		.st_gid = fGid,
		.st_atim = fAccessTime,
		.st_mtim = fModifyTime,
		.st_ctim = fChangeTime,
		.st_crtim = fCreateTime,
	};
	return B_OK;
}

status_t ShmfsVnode::WriteStat(const struct stat &stat, uint32 statMask)
{
	ino_t dirId;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("ShmfsVnode::WriteStat()\n");

	if ((statMask & B_STAT_MODE) != 0)
		fMode = stat.st_mode & S_IUMSK;
	if ((statMask & B_STAT_UID) != 0)
		fUid = stat.st_uid;
	if ((statMask & B_STAT_GID) != 0)
		fGid = stat.st_gid;
	if ((statMask & B_STAT_ACCESS_TIME) != 0)
		fAccessTime = stat.st_atim;
	if ((statMask & B_STAT_MODIFICATION_TIME) != 0)
		fModifyTime = stat.st_mtim;
	if ((statMask & B_STAT_CHANGE_TIME) != 0)
		fChangeTime = stat.st_ctim;
	else if (statMask != 0)
		GetCurrentTime(fChangeTime);
	if ((statMask & B_STAT_CREATION_TIME) != 0)
		fCreateTime = stat.st_crtim;

	dirId = fParent == NULL ? 0 : fParent->Id();
	}
	notify_stat_changed(Volume()->Id(), dirId, Id(), statMask);

	return B_OK;
}

status_t ShmfsVnode::Create(const char* name, int openMode, int perms, ShmfsFileCookie* &cookie, ino_t &newVnodeID)
{
	TRACE("ShmfsVnode::Create()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::Open(int openMode, ShmfsFileCookie* &cookie)
{
	TRACE("ShmfsVnode::Open()\n");
	cookie = NULL;
	return B_OK;
}

status_t ShmfsVnode::Close(ShmfsFileCookie* cookie)
{
	TRACE("ShmfsVnode::Close()\n");
	return B_OK;
}

status_t ShmfsVnode::FreeCookie(ShmfsFileCookie* cookie)
{
	TRACE("ShmfsVnode::FreeCookie()\n");
	return B_OK;
}

status_t ShmfsVnode::Read(ShmfsFileCookie* cookie, off_t pos, void* buffer, size_t &length)
{
	TRACE("ShmfsVnode::Read()\n");
	return B_IS_A_DIRECTORY;
}

status_t ShmfsVnode::Write(ShmfsFileCookie* cookie, off_t pos, const void* buffer, size_t &length)
{
	TRACE("ShmfsVnode::Write()\n");
	return B_IS_A_DIRECTORY;
}

status_t ShmfsVnode::CreateDir(const char* name, int perms)
{
	TRACE("ShmfsVnode::CreateDir()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::RemoveDir(const char* name)
{
	TRACE("ShmfsVnode::RemoveDir()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::OpenDir(ShmfsDirIterator* &cookie)
{
	TRACE("ShmfsVnode::OpenDir()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::CloseDir(ShmfsDirIterator* cookie)
{
	TRACE("ShmfsVnode::CloseDir()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::FreeDirCookie(ShmfsDirIterator* cookie)
{
	TRACE("ShmfsVnode::FreeDirCookie()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::ReadDir(ShmfsDirIterator* cookie, struct dirent* buffer, size_t bufferSize, uint32 &num)
{
	TRACE("ShmfsVnode::ReadDir()\n");
	return B_NOT_A_DIRECTORY;
}

status_t ShmfsVnode::RewindDir(ShmfsDirIterator* cookie)
{
	TRACE("ShmfsVnode::RewindDir()\n");
	return B_NOT_A_DIRECTORY;
}


//#pragma mark - Attributes

status_t ShmfsVnode::OpenAttrDir(ShmfsAttrDirIterator* &cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	cookie = new (std::nothrow) ShmfsAttrDirIterator();
	if (cookie == NULL)
		return B_NO_MEMORY;
	fAttrIterators.Insert(cookie);
	RewindAttrDir(cookie);
	return B_OK;
}

status_t ShmfsVnode::CloseAttrDir(ShmfsAttrDirIterator* cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	fAttrIterators.Remove(cookie);
	return B_OK;
}

status_t ShmfsVnode::FreeAttrDirCookie(ShmfsAttrDirIterator* cookie)
{
	delete cookie;
	return B_OK;
}

status_t ShmfsVnode::ReadAttrDir(ShmfsAttrDirIterator* cookie, struct dirent* buffer, size_t bufferSize, uint32 &num)
{
	RecursiveLocker lock(Volume()->Lock());

	const char *name;
	ShmfsAttribute *attr;
	uint32 maxNum = num;
	num = 0;

	for (;;) {
		if (!(num < maxNum))
			break;

		if (!AttrIteratorGet(cookie, name, attr))
			break;

		size_t direntSize = offsetof(struct dirent, d_name) + strlen(name) + 1;
		if (bufferSize < direntSize) {
			if (num == 0)
				return B_BUFFER_OVERFLOW;
			break;
		}
		*buffer = {
			.d_dev = Volume()->Id(),
			.d_ino = Id(),
			.d_reclen = (uint16)direntSize
		};
		strcpy(buffer->d_name, name);
		bufferSize -= direntSize;
		*(uint8**)&buffer += direntSize;
		num++;
		AttrIteratorNext(cookie);
	}

	return B_OK;
}

status_t ShmfsVnode::RewindAttrDir(ShmfsAttrDirIterator* cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	AttrIteratorRewind(cookie);
	return B_OK;
}

status_t ShmfsVnode::CreateAttr(const char* name, uint32 type, int openMode, ShmfsAttribute* &cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	ShmfsAttribute *oldAttr = fAttrs.Find(name);
	if (oldAttr != NULL) {
		if ((O_EXCL & openMode) != 0)
			return B_FILE_EXISTS;
		if ((O_TRUNC & openMode) != 0)
			CHECK_RET(oldAttr->WriteStat({.st_size = 0}, B_STAT_SIZE));
		oldAttr->AcquireReference();
		cookie = oldAttr;
		return B_OK;
	}
	BReference<ShmfsAttribute> attr(new(std::nothrow) ShmfsAttribute(), true);
	if (!attr.IsSet())
		return B_NO_MEMORY;
	CHECK_RET(attr->SetName(name));
	attr->fType = type;
	fAttrs.Insert(attr);
	attr->AcquireReference();
	cookie = attr.Detach();
	return B_OK;
}

status_t ShmfsVnode::OpenAttr(const char* name, int openMode, ShmfsAttribute* &cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	ShmfsAttribute *attr = fAttrs.Find(name);
	if (attr == NULL)
		return B_ENTRY_NOT_FOUND;
	attr->AcquireReference();
	cookie = attr;
	return B_OK;
}

status_t ShmfsVnode::CloseAttr(ShmfsAttribute* cookie)
{
	return B_OK;
}

status_t ShmfsVnode::FreeAttrCookie(ShmfsAttribute* cookie)
{
	cookie->ReleaseReference();
	return B_OK;
}

status_t ShmfsVnode::ReadAttr(ShmfsAttribute* cookie, off_t pos, void* buffer, size_t &length)
{
	return cookie->Read(pos, buffer, length);
}

status_t ShmfsVnode::WriteAttr(ShmfsAttribute* cookie, off_t pos, const void* buffer, size_t &length)
{
	return cookie->Write(pos, buffer, length);
}

status_t ShmfsVnode::ReadAttrStat(ShmfsAttribute* cookie, struct stat &stat)
{
	return cookie->ReadStat(stat);
}

status_t ShmfsVnode::WriteAttrStat(ShmfsAttribute* cookie, const struct stat &stat, int statMask)
{
	return cookie->WriteStat(stat, statMask);
}

status_t ShmfsVnode::RenameAttr(const char* fromName, ShmfsVnode* toVnode, const char* toName)
{
	RecursiveLocker lock(Volume()->Lock());

	ShmfsAttribute *attr = fAttrs.Find(fromName);
	if (attr == NULL)
		return B_ENTRY_NOT_FOUND;

	ShmfsAttribute* oldDstAttr = toVnode->fAttrs.Find(toName);
	if (oldDstAttr != NULL)
			CHECK_RET(toVnode->RemoveAttr(toName));

	RemoveAttr(attr);

	attr->SetName(toName);
	toVnode->fAttrs.Insert(attr);

	return B_OK;
}

status_t ShmfsVnode::RemoveAttr(const char* name)
{
	RecursiveLocker lock(Volume()->Lock());

	ShmfsAttribute *attr = fAttrs.Find(name);
	if (attr == NULL)
		return B_ENTRY_NOT_FOUND;

	RemoveAttr(attr);
	attr->ReleaseReference();

	return B_OK;
}
