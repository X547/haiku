#pragma once

#include "DeviceManager.h"

#include <util/AutoLock.h>
#include <util/AVLTree.h>
#include <util/DoublyLinkedList.h>
#include <util/KMessage.h>
#include <util/Vector.h>

#include "Utils.h"


class DriverModuleInfo;
class DriverAddonInfo;


class DriverCompatInfo {
private:
	DoublyLinkedListLink<DriverCompatInfo> fLink;

	typedef DoublyLinkedList<
		DriverCompatInfo, DoublyLinkedListMemberGetLink<DriverCompatInfo, &DriverCompatInfo::fLink>
	> List;

	struct MatchContext {
		DriverModuleInfo* moduleInfo {};
		float score = -1;
	};

public:
	~DriverCompatInfo();
	status_t Init(DriverAddonInfo* addonInfo, const KMessage& msg);

	void Match(DeviceNodeImpl* node);

private:
	void Match(DeviceNodeImpl* node, MatchContext ctx);

private:
	DriverModuleInfo* fModuleInfo {}; // owned by DriverAddonInfo
	float fScore = -1;
	KMessage fAttrs;
	DriverCompatInfo* fParentInfo {};
	List fChildInfos;
};


class DriverModuleInfo {
private:
	struct NameNodeDef {
		typedef const char* Key;
		typedef DriverModuleInfo Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fNameNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return &ContainerOf(*node, &DriverModuleInfo::fNameNode);
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			return strcmp(a, b->GetName());
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return strcmp(a->GetName(), b->GetName());
		}
	};

public:
	typedef AVLTree<NameNodeDef> NameMap;

public:
	status_t Init(DriverAddonInfo* addon, const char* name);

	const char* GetName() const {return fName.Get();}

private:
	DriverAddonInfo* fAddon {};
	AVLTreeNode fNameNode;
	CStringDeleter fName;
};


class DriverAddonInfo {
private:
	struct PathNodeDef {
		typedef const char* Key;
		typedef DriverAddonInfo Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fPathNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return &ContainerOf(*node, &DriverAddonInfo::fPathNode);
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			return strcmp(a, b->GetPath());
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return strcmp(a->GetPath(), b->GetPath());
		}
	};

public:
	typedef AVLTree<PathNodeDef> PathMap;

public:
	~DriverAddonInfo();
	status_t Init(const char* path, const KMessage& msg);
	status_t AddModule(const char* name, DriverModuleInfo*& outModule);

	const char* GetPath() const {return fPath.Get();}

private:
	friend class DriverRoster;

	AVLTreeNode fPathNode;
	CStringDeleter fPath;
	DriverModuleInfo::NameMap fModules;
	DriverCompatInfo fCompatInfo;
};


class DriverRoster {
public:
	static DriverRoster& Instance() {return sInstance;}
	status_t Init();

	void RegisterDeviceNode(DeviceNodeImpl* node);
	void UnregisterDeviceNode(DeviceNodeImpl* node);

private:
	status_t RegisterDriverAddon(DriverAddonInfo* driverAddon);
	void UnregisterDriverAddon(DriverAddonInfo* driverAddon);

private:
	static DriverRoster sInstance;

	mutex fLock = MUTEX_INITIALIZER("DriverRoster");
	DeviceNodeImpl::RosterList fDeviceNodes;
	DriverAddonInfo::PathMap fDriverAddons;
};
