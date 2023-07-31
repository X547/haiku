#pragma once

#include <dm2/device_manager.h>

#include <util/AVLTree.h>
#include <util/DoublyLinkedList.h>
#include <util/Vector.h>

#include "Utils.h"


class KMessage;
class DriverModuleInfo;
class DriverAddonInfo;


class DriverCompatInfo {
private:
	DoublyLinkedListLink<DriverCompatInfo> fLink;

	typedef DoublyLinkedList<
		DriverCompatInfo, DoublyLinkedListMemberGetLink<DriverCompatInfo, &DriverCompatInfo::fLink>
	> List;

	DriverModuleInfo* fModuleInfo {}; // owned by DriverAddonInfo
	float fScore = -1;
	ArrayDeleter<device_attr> fAttrs;
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
			return strcmp(a, b->fName.Get());
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return strcmp(a->fName.Get(), b->fName.Get());
		}
	};

public:
	typedef AVLTree<NameNodeDef> NameMap;

public:
	status_t Init(DriverAddonInfo* addon, const char* name);

private:
	AVLTreeNode fNameNode;

private:
	DriverAddonInfo* fAddon {};
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
			return strcmp(a, b->fPath.Get());
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			return strcmp(a->fPath.Get(), b->fPath.Get());
		}
	};

public:
	typedef AVLTree<PathNodeDef> PathMap;

public:
	status_t Init(const char* path, const KMessage& msg);

private:
	AVLTreeNode fPathNode;

private:
	CStringDeleter fPath;
	DriverModuleInfo::NameMap fModules;
	DriverCompatInfo fCompatInfo;
};


class DriverRoster {
public:
	struct LookupResult {
		float score;
		const char* module;
	};
	typedef Vector<LookupResult> LookupResultArray;


public:
	static DriverRoster& Instance() {return sInstance;}
	status_t Init();

	void Lookup(DeviceNode* node, LookupResultArray& result);

private:
	static DriverRoster sInstance;

	DriverAddonInfo::PathMap fDriverAddons;
};
