#pragma once

#include <dm2/device_manager.h>

#include <Referenceable.h>

#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <util/Vector.h>
#include <DPC.h>

#include "Utils.h"
#include "CompatDriverModuleList.h"
#include "DevFsNodeWrapper.h"


class DeviceNodeImpl;
class DevFsNodeWrapper;


class DeviceNodeImpl: public DeviceNode, public BReferenceable {
public:
	DeviceNodeImpl();
	virtual ~DeviceNodeImpl();

	int32 AcquireReference() final { return BReferenceable::AcquireReference(); }
	int32 ReleaseReference() final { return BReferenceable::ReleaseReference(); }

	DeviceNode* GetParent() const final;
	status_t GetNextChildNode(const device_attr* attrs, DeviceNode** node) const final;
	status_t FindChildNode(const device_attr* attrs, DeviceNode** node) const final;

	status_t GetNextAttr(const device_attr** attr) const final;
	status_t FindAttr(const char* name, type_code type, int32 index, const void** value, size_t* size) const final;

	void* QueryBusInterface(const char* ifaceName) final;
	void* QueryDriverInterface(const char* ifaceName, DeviceNode* dep) final;

	status_t InstallListener(DeviceNodeListener* listener) final;
	status_t UninstallListener(DeviceNodeListener* listener) final;

	status_t RegisterNode(DeviceNode* owner, BusDriver* driver, const device_attr* attrs, DeviceNode** node) final;
	status_t UnregisterNode(DeviceNode* node) final;

	status_t RegisterDevFsNode(const char* path, DevFsNode* driver) final;
	status_t UnregisterDevFsNode(const char* path) final;

	// Internal interface
	mutex* GetLock() {return &fLock;}
	const char* GetName() const;
	status_t Register(DeviceNodeImpl* parent, DeviceNodeImpl* owner, BusDriver* driver, const device_attr* attrs);
	status_t Probe();
	status_t ProbeDriver(const char* moduleName, bool isChild = false);
	void UnsetDeviceDriver();

	void InsertCompatDriverModule(DriverModuleInfo* module, float score);
	void RemoveCompatDriverModule(DriverModuleInfo* module);

private:
	void SetProbePending(bool doProbe);
	void UnregisterOwnedNodes(DeviceNodeImpl* owner);

private:
	DoublyLinkedListLink<DeviceNodeImpl> fLink;
	DoublyLinkedListLink<DeviceNodeImpl> fPendingLink;
	DoublyLinkedListLink<DeviceNodeImpl> fRosterLink;

public:
	typedef DoublyLinkedList<
		DeviceNodeImpl, DoublyLinkedListMemberGetLink<DeviceNodeImpl, &DeviceNodeImpl::fLink>
	> ChildList;
	typedef DoublyLinkedList<
		DeviceNodeImpl, DoublyLinkedListMemberGetLink<DeviceNodeImpl, &DeviceNodeImpl::fPendingLink>
	> PendingList;
	typedef DoublyLinkedList<
		DeviceNodeImpl, DoublyLinkedListMemberGetLink<DeviceNodeImpl, &DeviceNodeImpl::fRosterLink>
	> RosterList;

	ChildList& ChildNodes() {return fChildNodes;}

private:
	friend class DeviceManager;

	struct State {
		bool multipleDrivers: 1;
		bool registered: 1;
		bool unregistered: 1;
		bool probePending: 1;
		bool probed: 1;
	};

	mutex fLock = MUTEX_INITIALIZER("DeviceNode");
	State fState {};
	DeviceNodeImpl* fParent {};
	DeviceNodeImpl* fOwner {};
	ChildList fChildNodes;
	ArrayDeleter<device_attr> fAttributes;
	ArrayDeleter<uint8> fAttrData;

	CompatDriverModuleList fCompatDriverModules;

	BusDriver* fBusDriver {};
	DeviceDriver* fDeviceDriver {};
	CStringDeleter fDriverModuleName;

	DevFsNodeWrapper::List fDevFsNodes;
};


class DeviceManager: private DPCCallback {
public:
	static DeviceManager& Instance() {return sInstance;}

	status_t Init();
	DeviceNode* GetRootNode() const;
	DeviceNodeImpl* GetRootNodeNoRef() const {return fRoot;}
	void SetRootNode(DeviceNodeImpl* node);

	recursive_lock* GetLock() {return &fLock;}

	DeviceNodeImpl::PendingList& PendingNodes() {return fPendingList;}
	void ScheduleProbe();
	status_t ProcessPendingNodes();

	void DumpTree();
	void RunTest(const char* testName);

private:
	void DumpNode(DeviceNodeImpl* node, int32 level);

	// DPCCallback
	void DoDPC(DPCQueue* queue) final;

private:
	static DeviceManager sInstance;

	recursive_lock fLock = RECURSIVE_LOCK_INITIALIZER("DeviceManager");
	bool fIsDpcEnqueued = false;

	DeviceNodeImpl* fRoot {};
	DeviceNodeImpl::PendingList fPendingList;
};
