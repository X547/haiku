#pragma once

#include <dm2/device_manager.h>

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
public:
	status_t Init(DriverAddonInfo* addon, const char* name);

private:
	DoublyLinkedListLink<DriverModuleInfo> fLink;

public:
	typedef DoublyLinkedList<
		DriverModuleInfo, DoublyLinkedListMemberGetLink<DriverModuleInfo, &DriverModuleInfo::fLink>
	> List;

private:
	DriverAddonInfo* fAddon {};
	CStringDeleter fName;
};


class DriverAddonInfo {
public:
	status_t Init(const char* path, const KMessage& msg);

private:
	DoublyLinkedListLink<DriverAddonInfo> fLink;

public:
	typedef DoublyLinkedList<
		DriverAddonInfo, DoublyLinkedListMemberGetLink<DriverAddonInfo, &DriverAddonInfo::fLink>
	> List;

private:
	CStringDeleter fPath;
	// TODO: AVL by module name
	DriverModuleInfo::List fModules;
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

	// TODO: AVL by add-on path
	DriverAddonInfo::List fDriverAddons;
};
