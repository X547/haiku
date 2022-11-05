#include "Shmfs.h"

#include <KernelExport.h>
#include <NodeMonitor.h>
#include <dirent.h>

#include <util/AutoLock.h>

#include <new>
#include <algorithm>


ShmfsDirectoryVnode::~ShmfsDirectoryVnode()
{
	for (;;) {
		ShmfsVnode *vnode = fNodes.LeftMost();
		if (vnode == NULL)
			break;
		fNodes.Remove(vnode);
		vnode->ReleaseReference();
	}
}


void ShmfsDirectoryVnode::IteratorRewind(ShmfsDirIterator* cookie)
{
	cookie->idx = 0;
	cookie->node = fNodes.LeftMost();
}

bool ShmfsDirectoryVnode::IteratorGet(ShmfsDirIterator* cookie, const char *&name, ShmfsVnode *&vnode)
{
	switch (cookie->idx) {
		case 0:
			name = ".";
			vnode = this;
			return true;
		case 1:
			if (fParent == NULL) {
				cookie->idx++;
				return IteratorGet(cookie, name, vnode);
			} else {
				name = "..";
				vnode = fParent;
				return true;
			}
		case 2:
			if (cookie->node == NULL)
				return false;
			vnode = cookie->node;
			name = vnode->Name();
			return true;
	}
	return false;
}

void ShmfsDirectoryVnode::IteratorNext(ShmfsDirIterator* cookie)
{
	switch (cookie->idx) {
		case 0:
			cookie->idx++;
			if (fParent == NULL)
				cookie->idx++;
			break;
		case 1:
			cookie->idx++;
			break;
		case 2:
			cookie->node = fNodes.Next(cookie->node);
			break;
	}
}

void ShmfsDirectoryVnode::InitTimestamps(ShmfsVnode *vnode)
{
	struct timespec time;
	GetCurrentTime(time);
	vnode->fAccessTime = time;
	vnode->fModifyTime = time;
	vnode->fChangeTime = time;
	vnode->fCreateTime = time;

	fModifyTime = time;
	fChangeTime = time;
}

void ShmfsDirectoryVnode::RemoveNode(ShmfsVnode *vnode)
{
	for (ShmfsDirIterator *it = fIterators.First(); it != NULL; it = fIterators.GetNext(it)) {
		if (it->node == vnode)
			IteratorNext(it);
	}
	fNodes.Remove(vnode);
}


//#pragma mark - VFS interface

status_t ShmfsDirectoryVnode::CreateSymlink(const char* name, const char* path, int mode)
{
	ino_t id;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::CreateSymlink(\"%s\", \"%s\")\n", Id(), name, path);

	if (fNodes.Find(name))
		return B_FILE_EXISTS;

	BReference<ShmfsSymlinkVnode> vnode(new (std::nothrow) ShmfsSymlinkVnode(), true);
	if (!vnode.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(vnode->SetName(name));
	CHECK_RET(vnode->SetPath(path));
	vnode->fParent = this;
	vnode->fMode = mode & S_IUMSK;
	CHECK_RET(Volume()->RegisterVnode(vnode));
	id = vnode->Id();

	InitTimestamps(vnode.Get());
	fNodes.Insert(vnode);
	vnode.Detach();
	}

	notify_entry_created(Volume()->Id(), Id(), name, id);

	return B_OK;
}

status_t ShmfsDirectoryVnode::Unlink(const char* name)
{
	ino_t id;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::Unlink(\"%s\")\n", Id(), name);

	ShmfsVnode *vnode = fNodes.Find(name);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	if (dynamic_cast<ShmfsDirectoryVnode*>(vnode) != NULL)
		return B_IS_A_DIRECTORY;

	id = vnode->Id();
	RemoveNode(vnode);
	if (acquire_vnode(Volume()->Base(), vnode->Id()) >= B_OK) {
		remove_vnode(Volume()->Base(), vnode->Id());
		put_vnode(Volume()->Base(), vnode->Id());
	}
	vnode->ReleaseReference();
	}
	notify_entry_removed(Volume()->Id(), Id(), name, id);

	return B_OK;
}

status_t ShmfsDirectoryVnode::Rename(const char* fromName, ShmfsVnode* toDir, const char* toName)
{
	ino_t id, srcDirId, dstDirId;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::Rename()\n", Id());

	ShmfsDirectoryVnode *dstDirVnode = dynamic_cast<ShmfsDirectoryVnode*>(toDir);
	if (dstDirVnode == NULL)
		return B_NOT_A_DIRECTORY;

	ShmfsVnode *vnode = fNodes.Find(fromName);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	ShmfsVnode* oldDstVnode = dstDirVnode->fNodes.Find(toName);
	if (oldDstVnode != NULL) {
		if (dynamic_cast<ShmfsDirectoryVnode*>(oldDstVnode) != NULL) {
			CHECK_RET(dstDirVnode->RemoveDir(toName));
		} else {
			CHECK_RET(dstDirVnode->Unlink(toName));
		}
	}

	RemoveNode(vnode);
	vnode->SetName(toName);
	dstDirVnode->fNodes.Insert(vnode);

	id = vnode->Id();
	srcDirId = Id();
	dstDirId = dstDirVnode->Id();
	}
	notify_entry_moved(Volume()->Id(), srcDirId, fromName, dstDirId, toName, id);

	return B_OK;
}

status_t ShmfsDirectoryVnode::ReadStat(struct stat &stat)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::ReadStat()\n", Id());
	CHECK_RET(ShmfsVnode::ReadStat(stat));
	stat.st_mode |= S_IFDIR;
	return B_OK;
}

status_t ShmfsDirectoryVnode::Create(const char* name, int openMode, int perms, ShmfsFileCookie* &cookie, ino_t &newVnodeID)
{
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::Create()\n", Id());

	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return B_IS_A_DIRECTORY;

	ShmfsVnode *oldVnode = fNodes.Find(name);
	if (oldVnode != NULL) {
		if (dynamic_cast<ShmfsDirectoryVnode*>(oldVnode) != NULL)
			return B_IS_A_DIRECTORY;
		if ((O_EXCL & openMode) != 0)
			return B_FILE_EXISTS;
		CHECK_RET(oldVnode->Open(openMode, cookie));
		newVnodeID = oldVnode->Id();
		return B_OK;
	}

	BReference<ShmfsFileVnode> vnode(new (std::nothrow) ShmfsFileVnode(), true);
	if (!vnode.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(vnode->SetName(name));
	vnode->fParent = this;
	vnode->fMode = perms & S_IUMSK;
	CHECK_RET(vnode->Open(openMode, cookie));
	CHECK_RET(Volume()->RegisterVnode(vnode));
	newVnodeID = vnode->Id();
	CHECK_RET(get_vnode(Volume()->Base(), vnode->Id(), NULL));
	status_t res = vnode->Init();
	if (res < B_OK) {
		put_vnode(Volume()->Base(), vnode->Id());
		vnode.Detach();
		return res;
	}
	InitTimestamps(vnode.Get());
	fNodes.Insert(vnode);
	vnode.Detach();
	}
	notify_entry_created(Volume()->Id(), Id(), name, newVnodeID);

	return B_OK;
}

status_t ShmfsDirectoryVnode::CreateDir(const char* name, int perms)
{
	ino_t id;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::CreateDir()\n", Id());

	if (fNodes.Find(name) || strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return B_FILE_EXISTS;

	BReference<ShmfsDirectoryVnode> vnode(new (std::nothrow) ShmfsDirectoryVnode(), true);
	if (!vnode.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(vnode->SetName(name));
	vnode->fParent = this;
	vnode->fMode = perms & S_IUMSK;
	CHECK_RET(Volume()->RegisterVnode(vnode));
	id = vnode->Id();

	InitTimestamps(vnode.Get());
	fNodes.Insert(vnode);
	vnode.Detach();
	}
	notify_entry_created(Volume()->Id(), Id(), name, id);

	return B_OK;
}

status_t ShmfsDirectoryVnode::RemoveDir(const char* name)
{
	ino_t id;
	{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::RemoveDir(\"%s\")\n", Id(), name);

	ShmfsVnode *vnode = fNodes.Find(name);
	if (vnode == NULL)
		return B_ENTRY_NOT_FOUND;

	ShmfsDirectoryVnode *dirVnode = dynamic_cast<ShmfsDirectoryVnode*>(vnode);
	if (dirVnode == NULL)
		return B_NOT_A_DIRECTORY;

	if (!dirVnode->fNodes.IsEmpty())
		return B_DIRECTORY_NOT_EMPTY;

	id = dirVnode->Id();
	RemoveNode(dirVnode);
	dirVnode->ReleaseReference();
	remove_vnode(Volume()->Base(), vnode->Id());
	}
	notify_entry_removed(Volume()->Id(), Id(), name, id);

	return B_OK;
}

status_t ShmfsDirectoryVnode::Lookup(const char* name, ino_t &id)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".ShmfsDirectoryVnode::Lookup(\"%s\")\n", Id(), name);
	if (strcmp(name, ".") == 0) {
		id = Id();
	} else if (strcmp(name, "..") == 0) {
		if (fParent == NULL)
			return B_ENTRY_NOT_FOUND;
		id = fParent->Id();
	} else {
		ShmfsVnode *vnode = fNodes.Find(name);
		if (vnode == NULL)
			return B_ENTRY_NOT_FOUND;
		id = vnode->Id();
	}
	TRACE("  id: %" B_PRId64 "\n", id);
	CHECK_RET(get_vnode(Volume()->Base(), id, NULL));
	return B_OK;
}

status_t ShmfsDirectoryVnode::OpenDir(ShmfsDirIterator* &cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::OpenDir()\n", Id());
	cookie = new (std::nothrow) ShmfsDirIterator();
	if (cookie == NULL)
		return B_NO_MEMORY;
	fIterators.Insert(cookie);
	IteratorRewind(cookie);
	return B_OK;
}

status_t ShmfsDirectoryVnode::CloseDir(ShmfsDirIterator* cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::CloseDir()\n", Id());
	fIterators.Remove(cookie);
	return B_OK;
}

status_t ShmfsDirectoryVnode::FreeDirCookie(ShmfsDirIterator* cookie)
{
	TRACE("#%" B_PRId64 ".DirectoryVnode::FreeDirCookie()\n", Id());
	delete cookie;
	return B_OK;
}

status_t ShmfsDirectoryVnode::ReadDir(ShmfsDirIterator* cookie, struct dirent* buffer, size_t bufferSize, uint32 &num)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::ReadDir()\n", Id());

	const char *name;
	ShmfsVnode *vnode;
	uint32 maxNum = num;
	num = 0;

	for (;;) {
		if (!(num < maxNum))
			break;

		if (!IteratorGet(cookie, name, vnode))
			break;

		size_t direntSize = offsetof(struct dirent, d_name) + strlen(name) + 1;
		if (bufferSize < direntSize) {
			if (num == 0)
				return B_BUFFER_OVERFLOW;
			break;
		}
		*buffer = {
			.d_dev = Volume()->Id(),
			.d_ino = vnode->Id(),
			.d_reclen = (uint16)direntSize
		};
		strcpy(buffer->d_name, name);
		bufferSize -= direntSize;
		*(uint8**)&buffer += direntSize;
		num++;
		IteratorNext(cookie);
	}

	return B_OK;
}

status_t ShmfsDirectoryVnode::RewindDir(ShmfsDirIterator* cookie)
{
	RecursiveLocker lock(Volume()->Lock());
	TRACE("#%" B_PRId64 ".DirectoryVnode::RewindDir()\n", Id());
	IteratorRewind(cookie);
	return B_OK;
}
