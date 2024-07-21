#pragma once

#include <dm2/device_manager.h>

#include <Referenceable.h>

#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <util/Vector.h>
#include <condition_variable.h>
#include <DPC.h>

#include "Utils.h"
#include "CompatDriverModuleList.h"
#include "DevFsNodeWrapper.h"


class DeviceNodeImpl;
class DevFsNodeWrapper;


class DriverDependencyImpl final: public DriverDependency {
public:
	DriverDependencyImpl(DeviceNodeImpl* source, DeviceNodeImpl* target);
	void Free() final;

private:
	DoublyLinkedListLink<DriverDependencyImpl> fSourceLink;
	DoublyLinkedListLink<DriverDependencyImpl> fTargetLink;
	DeviceNodeImpl* fSource;
	DeviceNodeImpl* fTarget;

public:
	typedef DoublyLinkedList<
		DriverDependencyImpl, DoublyLinkedListMemberGetLink<DriverDependencyImpl, &DriverDependencyImpl::fSourceLink>
	> SourceList;
	typedef DoublyLinkedList<
		DriverDependencyImpl, DoublyLinkedListMemberGetLink<DriverDependencyImpl, &DriverDependencyImpl::fTargetLink>
	> TargetList;
};


class DeviceNodeImpl: public DeviceNode, public BReferenceable {
public:
	DeviceNodeImpl();
	virtual ~DeviceNodeImpl();

	int32 Id() final { return fId; }

	int32 AcquireReference() final { return BReferenceable::AcquireReference(); }
	int32 ReleaseReference() final { return BReferenceable::ReleaseReference(); }

	DeviceNode* GetParent() const final;
	status_t GetNextChildNode(const device_attr* attrs, DeviceNode** node) const final;
	status_t FindChildNode(const device_attr* attrs, DeviceNode** node) const final;

	status_t GetNextAttr(const device_attr** attr) const final;
	status_t FindAttr(const char* name, type_code type, int32 index, const void** value, size_t* size) const final;

	void* QueryBusInterface(const char* ifaceName) final;
	void* QueryDriverInterface(const char* ifaceName) final;

	status_t InstallListener(DeviceNodeListener* listener) final;
	status_t UninstallListener(DeviceNodeListener* listener) final;

	status_t AddDependency(DeviceNode* node, DriverDependency::Flags flags, DriverDependency** dep);

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
	friend class DriverDependencyImpl;

	struct State {
		bool multipleDrivers: 1;
		bool registered: 1;
		bool unregistered: 1;
		bool probePending: 1;
		bool probed: 1;
		bool inProbe: 1;
		bool driverAttached: 1;
	};

	mutable mutex fLock = MUTEX_INITIALIZER("DeviceNode");
	int32 fId = -1;
	State fState {};
	ConditionVariable fProbeCompletedCond;
	DeviceNodeImpl* fParent {};
	DeviceNodeImpl* fOwner {};
	ChildList fChildNodes;
	ArrayDeleter<device_attr> fAttributes;
	ArrayDeleter<uint8> fAttrData;
	DriverDependencyImpl::SourceList fDepSourceList;
	DriverDependencyImpl::TargetList fDepTargetList;

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
	DeviceNodeImpl* GetRootNode() const;
	DeviceNodeImpl* GetRootNodeNoRef() const {return fRoot;}
	void SetRootNode(DeviceNodeImpl* node);

	DeviceNodeImpl::PendingList& PendingNodes() {return fPendingList;}
	void AddToProbePendingList(DeviceNodeImpl* node, bool doAdd);
	void LockProbe();
	void UnlockProbe();
	status_t ProbeFence();

	void DumpTree();
	void RunTest(const char* testName);

private:
	void DumpNode(DeviceNodeImpl* node, int32 level);

	// DPCCallback
	void DoDPC(DPCQueue* queue) final;

private:
	static DeviceManager sInstance;

	mutable mutex fLock = MUTEX_INITIALIZER("DeviceManager");
	int32 fProbeLockCount = 0;
	DPCQueue* fDPCQueue = DPCQueue::DefaultQueue(B_LOW_PRIORITY);

	DeviceNodeImpl* fRoot {};
	DeviceNodeImpl::PendingList fPendingList;
	ConditionVariable fPendingListEmptyCond;
};
