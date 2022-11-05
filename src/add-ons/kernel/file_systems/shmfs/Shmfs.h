#pragma once

#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include <fs_interface.h>
#include <lock.h>
#include <Referenceable.h>
#include <AutoDeleter.h>
#include <util/AVLTree.h>
#include <util/DoublyLinkedList.h>

#include <string.h>

#include "ExternalAllocator.h"

#if 0
#define TRACE(x...) dprintf(x)
#else
#define TRACE(x...) ;
#endif

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class VMCache;
class vm_page;

class ShmfsVolume;
class ShmfsVnode;
class ShmfsFileCookie;
class ShmfsDirIterator;
class ShmfsAttrDirIterator;


void GetCurrentTime(struct timespec &outTime);


class ShmfsAttribute: public BReferenceable {
private:
	ArrayDeleter<char> fName;
public:
	int32 fType = 0;
private:
	uint32 fDataSize = 0, fDataAllocSize = 0;
	ArrayDeleter<uint8> fData;
	AVLTreeNode fNameNode;

	struct NameNodeDef {
		typedef const char *Key;
		typedef ShmfsAttribute Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fNameNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return (Value*)((char*)node - offsetof(Value, fNameNode));
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			return strcmp(a, &b->fName[0]);
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return strcmp(&a->fName[0], &b->fName[0]);
		}
	};

public:
	typedef AVLTree<NameNodeDef> NameMap;

private:
	status_t EnsureSize(uint32 size);

public:
	const char* Name() {return !fName.IsSet() ? "" : &fName[0];}
	status_t SetName(const char* name);

	status_t Read(off_t pos, void* buffer, size_t &length);
	status_t Write(off_t pos, const void* buffer, size_t &length);
	status_t ReadStat(struct stat &stat);
	status_t WriteStat(const struct stat &stat, int statMask);
};


struct ShmfsAttrDirIterator {
	DoublyLinkedListLink<ShmfsAttrDirIterator> link;
	ShmfsAttribute* attr;

	typedef DoublyLinkedList<
		ShmfsAttrDirIterator,
		DoublyLinkedListMemberGetLink<ShmfsAttrDirIterator, &ShmfsAttrDirIterator::link>
	> List;
};


class ShmfsVnode: public BReferenceable {
private:
	friend class ShmfsVolume;

	ShmfsVolume *fVolume{};
	ino_t fId = 0;
	ArrayDeleter<char> fName;
	AVLTreeNode fIdNode;
	AVLTreeNode fNameNode;

	ShmfsAttribute::NameMap fAttrs;
	ShmfsAttrDirIterator::List fAttrIterators;

public:
	ShmfsVnode *fParent{};

	// stat structure
	uid_t fUid{};
	gid_t fGid{};
	mode_t fMode{};
	struct timespec fAccessTime{};
	struct timespec fModifyTime{};
	struct timespec fChangeTime{};
	struct timespec fCreateTime{};

private:
	struct IdNodeDef {
		typedef ino_t Key;
		typedef ShmfsVnode Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fIdNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return (Value*)((char*)node - offsetof(Value, fIdNode));
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			return (a < b->fId) ? -1 : (a > b->fId) ? 1 : 0;
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return (a->fId < b->fId) ? -1 : (a->fId > b->fId) ? 1 : 0;
		}
	};

	struct NameNodeDef {
		typedef const char *Key;
		typedef ShmfsVnode Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fNameNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return (Value*)((char*)node - offsetof(Value, fNameNode));
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			return strcmp(a, &b->fName[0]);
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return strcmp(&a->fName[0], &b->fName[0]);
		}
	};

public:
	typedef AVLTree<IdNodeDef> IdMap;
	typedef AVLTree<NameNodeDef> NameMap;

private:
	void AttrIteratorRewind(ShmfsAttrDirIterator* cookie);
	bool AttrIteratorGet(ShmfsAttrDirIterator* cookie, const char *&name, ShmfsAttribute *&attr);
	void AttrIteratorNext(ShmfsAttrDirIterator* cookie);
	void RemoveAttr(ShmfsAttribute *attr);

public:
	virtual ~ShmfsVnode();

	inline ino_t Id() {return fId;}
	inline ShmfsVolume *Volume() {return fVolume;}
	inline const char *Name() {return !fName.IsSet() ? "" : &fName[0];}
	status_t SetName(const char *name);

	virtual status_t Lookup(const char* name, ino_t &id);
	status_t GetVnodeName(char* buffer, size_t bufferSize);
	status_t PutVnode(bool reenter);
	status_t RemoveVnode(bool reenter);
	status_t Ioctl(ShmfsFileCookie* cookie, uint32 op, void* buffer, size_t length);
	virtual status_t SetFlags(ShmfsFileCookie* cookie, int flags);
	status_t Fsync();
	virtual status_t ReadSymlink(char* buffer, size_t &bufferSize);
	virtual status_t CreateSymlink(const char* name, const char* path, int mode);
	virtual status_t Unlink(const char* name);
	virtual status_t Rename(const char* fromName, ShmfsVnode* toDir, const char* toName);
	status_t Access(int mode);
	virtual status_t ReadStat(struct stat &stat);
	virtual status_t WriteStat(const struct stat &stat, uint32 statMask);
	virtual status_t Create(const char* name, int openMode, int perms, ShmfsFileCookie* &cookie, ino_t &newVnodeID);
	virtual status_t Open(int openMode, ShmfsFileCookie* &cookie);
	status_t Close(ShmfsFileCookie* cookie);
	virtual status_t FreeCookie(ShmfsFileCookie* cookie);
	virtual status_t Read(ShmfsFileCookie* cookie, off_t pos, void* buffer, size_t &length);
	virtual status_t Write(ShmfsFileCookie* cookie, off_t pos, const void* buffer, size_t &length);
	virtual status_t CreateDir(const char* name, int perms);
	virtual status_t RemoveDir(const char* name);
	virtual status_t OpenDir(ShmfsDirIterator* &cookie);
	virtual status_t CloseDir(ShmfsDirIterator* cookie);
	virtual status_t FreeDirCookie(ShmfsDirIterator* cookie);
	virtual status_t ReadDir(ShmfsDirIterator* cookie, struct dirent* buffer, size_t bufferSize, uint32 &num);
	virtual status_t RewindDir(ShmfsDirIterator* cookie);

	status_t OpenAttrDir(ShmfsAttrDirIterator* &cookie);
	status_t CloseAttrDir(ShmfsAttrDirIterator* cookie);
	status_t FreeAttrDirCookie(ShmfsAttrDirIterator* cookie);
	status_t ReadAttrDir(ShmfsAttrDirIterator* cookie, struct dirent* buffer, size_t bufferSize, uint32 &num);
	status_t RewindAttrDir(ShmfsAttrDirIterator* cookie);
	status_t CreateAttr(const char* name, uint32 type, int openMode, ShmfsAttribute* &cookie);
	status_t OpenAttr(const char* name, int openMode, ShmfsAttribute* &cookie);
	status_t CloseAttr(ShmfsAttribute* cookie);
	status_t FreeAttrCookie(ShmfsAttribute* cookie);
	status_t ReadAttr(ShmfsAttribute* cookie, off_t pos, void* buffer, size_t &length);
	status_t WriteAttr(ShmfsAttribute* cookie, off_t pos, const void* buffer, size_t &length);
	status_t ReadAttrStat(ShmfsAttribute* cookie, struct stat &stat);
	status_t WriteAttrStat(ShmfsAttribute* cookie, const struct stat &stat, int statMask);
	status_t RenameAttr(const char* fromName, ShmfsVnode* toVnode, const char* toName);
	status_t RemoveAttr(const char* name);
};

class ShmfsFileVnode: public ShmfsVnode {
private:
	VMCache* fCache{};
	uint64 fDataSize = 0;

private:
	void _GetPages(off_t offset, off_t length, bool isWrite, vm_page** pages);
	void _PutPages(off_t offset, off_t length, vm_page** pages, bool success);
	status_t _DoCacheIO(const off_t offset, uint8* buffer, ssize_t length, size_t &bytesProcessed, bool isWrite);

public:
	~ShmfsFileVnode();

	status_t Init();

	status_t SetFlags(ShmfsFileCookie* cookie, int flags) final;
	status_t ReadStat(struct stat &stat) final;
	status_t WriteStat(const struct stat &stat, uint32 statMask) final;
	status_t Open(int openMode, ShmfsFileCookie* &cookie) final;
	status_t FreeCookie(ShmfsFileCookie* cookie) final;
	status_t Read(ShmfsFileCookie* cookie, off_t pos, void* buffer, size_t &length) final;
	status_t Write(ShmfsFileCookie* cookie, off_t pos, const void* buffer, size_t &length) final;
};


struct ShmfsDirIterator {
	DoublyLinkedListLink<ShmfsDirIterator> link;
	int32 idx;
	ShmfsVnode* node;

	typedef DoublyLinkedList<
		ShmfsDirIterator,
		DoublyLinkedListMemberGetLink<ShmfsDirIterator, &ShmfsDirIterator::link>
	> List;
};


class ShmfsDirectoryVnode: public ShmfsVnode {
private:
	ShmfsVnode::NameMap fNodes;
	ShmfsDirIterator::List fIterators;

	void IteratorRewind(ShmfsDirIterator* cookie);
	bool IteratorGet(ShmfsDirIterator* cookie, const char *&name, ShmfsVnode *&vnode);
	void IteratorNext(ShmfsDirIterator* cookie);

	void InitTimestamps(ShmfsVnode* vnode);
	void RemoveNode(ShmfsVnode *vnode);

public:
	~ShmfsDirectoryVnode();

	status_t CreateSymlink(const char* name, const char* path, int mode) final;
	status_t Unlink(const char* name) final;
	status_t Rename(const char* fromName, ShmfsVnode* toDir, const char* toName) final;
	status_t ReadStat(struct stat &stat) final;
	status_t Create(const char* name, int openMode, int perms, ShmfsFileCookie* &cookie, ino_t &newVnodeID) final;
	status_t CreateDir(const char* name, int perms) final;
	status_t RemoveDir(const char* name) final;
	status_t Lookup(const char* name, ino_t &id) final;
	status_t OpenDir(ShmfsDirIterator* &cookie) final;
	status_t CloseDir(ShmfsDirIterator* cookie) final;
	status_t FreeDirCookie(ShmfsDirIterator* cookie) final;
	status_t ReadDir(ShmfsDirIterator* cookie, struct dirent* buffer, size_t bufferSize, uint32 &num) final;
	status_t RewindDir(ShmfsDirIterator* cookie) final;
};


class ShmfsSymlinkVnode: public ShmfsVnode {
private:
	ArrayDeleter<char> fPath;

public:
	~ShmfsSymlinkVnode() = default;

	const char* GetPath() {return !fPath.IsSet() ? "" : &fPath[0];}
	status_t SetPath(const char* path);

	status_t ReadStat(struct stat &stat) final;
	status_t ReadSymlink(char* buffer, size_t &bufferSize) final;
};


class ShmfsVolume {
private:
	friend class ShmfsVnode;

	recursive_lock fLock = RECURSIVE_LOCK_INITIALIZER("shmfs volume");

	fs_volume *fBase{};

	BReference<ShmfsVnode> fRootVnode;
	ShmfsVnode::IdMap fIds;
	ExternalAllocator fIdPool;

	void ListVnodes();

public:
	ShmfsVolume();

	inline recursive_lock *Lock() {return &fLock;}

	inline fs_volume *Base() {return fBase;}
	inline dev_t Id() {return fBase->id;}

	status_t RegisterVnode(ShmfsVnode *vnode);

	static status_t Mount(ShmfsVolume* &volume, fs_volume *base, const char* device, uint32 flags, const char* args, ino_t &_rootVnodeID);
	status_t Unmount();
	status_t ReadFsInfo(struct fs_info &info);
	status_t GetVnode(ino_t id, ShmfsVnode* &vnode, int &type, uint32 &flags, bool reenter);
};
