#pragma once

#include <dm2/device_manager.h>

#include <Referenceable.h>

#include <util/DoublyLinkedList.h>
#include <util/Vector.h>

#include "Utils.h"
#include "CompatDriverModuleList.h"


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

	status_t GetNextAttr(device_attr** attr) const final;
	status_t FindAttr(const char* name, type_code type, int32 index, const void** value, size_t* size) const final;

	void* QueryBusInterface(const char* ifaceName) final;
	void* QueryDriverInterface(const char* ifaceName) final;

	status_t InstallListener(DeviceNodeListener* listener) final;
	status_t UninstallListener(DeviceNodeListener* listener) final;

	status_t RegisterNode(BusDriver* driver, DeviceNode** node) final;
	status_t UnregisterNode(DeviceNode* node) final;

	status_t RegisterDevFsNode(const char* path, DevFsNode* driver) final;
	status_t UnregisterDevFsNode(const char* path) final;

	// Internal interface
	const char* GetName() const;
	status_t Register(DeviceNodeImpl* parent, BusDriver* driver);
	status_t Probe();
	status_t ProbeDriver(const char* moduleName, bool isChild = false);
	void UnsetDeviceDriver();

	void InsertCompatDriverModule(DriverModuleInfo* module, float score);
	void RemoveCompatDriverModule(DriverModuleInfo* module);

private:
	void SetProbePending(bool doProbe);

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

	union State {
		struct {
			uint32 multipleDrivers: 1;
			uint32 registered: 1;
			uint32 unregistered: 1;
			uint32 probePending: 1;
			uint32 probed: 1;
			uint32 unused: 27;
		};
		uint32 val;
	};

	State fState {};
	DeviceNodeImpl* fParent {};
	ChildList fChildNodes;

	CompatDriverModuleList fCompatDriverModules;

	BusDriver* fBusDriver {};
	DeviceDriver* fDeviceDriver {};
	CStringDeleter fDriverModuleName;

	Vector<DevFsNodeWrapper*> fDevFsNodes;
};


class DeviceManager {
public:
	static DeviceManager& Instance() {return sInstance;}

	status_t Init();
	status_t FileSystemMounted();
	DeviceNode* GetRootNode() const;
	void SetRootNode(DeviceNodeImpl* node);

	DeviceNodeImpl::PendingList& PendingNodes() {return fPendingList;}
	status_t ProcessPendingNodes();

	void DumpTree();

private:
	void DumpNode(DeviceNodeImpl* node, int32 level);

private:
	static DeviceManager sInstance;

	DeviceNodeImpl* fRoot {};
	DeviceNodeImpl::PendingList fPendingList;
};
