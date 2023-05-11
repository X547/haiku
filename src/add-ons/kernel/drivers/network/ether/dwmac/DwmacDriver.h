#pragma once

#include <device_manager.h>

#include <lock.h>
#include <util/AVLTree.h>

#include "CppUtils.h"


#define DWMAC_DEVICE_ID_GENERATOR	"dwmac/device_id"


class DwmacNetDevice;


class DwmacDriver {
public:
	struct IdNodeDef {
		typedef int32 Key;
		typedef DwmacDriver Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->fIdNode;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return &ContainerOf(*node, &DwmacDriver::fIdNode);
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			if (a < b->fId) return -1;
			if (a > b->fId) return 1;
			return 0;
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			if (a->fId < b->fId) return -1;
			if (a->fId > b->fId) return 1;
			return 0;
		}
	};

	typedef AVLTree<IdNodeDef> IdMap;

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, DwmacDriver*& driver);
	void UninitDriver();
	status_t RegisterChildDevices();

	DwmacNetDevice* NetDevice() {return fNetDevice;}
	void SetNetDevice(DwmacNetDevice* netDevice) {fNetDevice = netDevice;}

private:
	device_node* fNode {};
	int32 fId = -1;
	AVLTreeNode fIdNode;
	DwmacNetDevice* fNetDevice {};

	inline status_t InitDriverInt(device_node* node);
};

class DwmacDevice {
};

class DwmacRoster {
public:
	static DwmacRoster& Instance() {return sInstance;}

	inline recursive_lock& Lock() {return fLock;}

	void Insert(DwmacDriver* driver) {fDrivers.Insert(driver);}
	void Remove(DwmacDriver* driver) {fDrivers.Remove(driver);}
	DwmacDriver* Lookup(int32 id) {return fDrivers.Find(id);}

private:
	static DwmacRoster sInstance;

	recursive_lock fLock = RECURSIVE_LOCK_INITIALIZER("DwmacRoster");
	DwmacDriver::IdMap fDrivers;
};
