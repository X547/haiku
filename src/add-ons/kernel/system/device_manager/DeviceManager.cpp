#include <dm2/device_manager.h>

#include <string.h>

#include <Referenceable.h>

#include <AutoDeleter.h>
#include <ScopeExit.h>
#include <HashMap.h>
#include <util/DoublyLinkedList.h>

#include "devfs_private.h"

#include "DriverRoster.h"
#include "RootDevice.h"
#include "DevFsNodeWrapper.h"


// TODO: locking
// TODO: check ownership management


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


struct BusDriverDeleter : MethodDeleter<BusDriver, void, &BusDriver::Free>
{
	typedef MethodDeleter<BusDriver, void, &BusDriver::Free> Base;

	BusDriverDeleter() : Base() {}
	BusDriverDeleter(BusDriver* object) : Base(object) {}
};


class DeviceNodeImpl;


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
	status_t FindAttr(const char* name, type_code type, int32 index, const void** value) const final;

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

private:
	void SetProbe(bool doProbe);

private:
	DoublyLinkedListLink<DeviceNodeImpl> fLink;
	DoublyLinkedListLink<DeviceNodeImpl> fPendingLink;

public:
	typedef DoublyLinkedList<
		DeviceNodeImpl, DoublyLinkedListMemberGetLink<DeviceNodeImpl, &DeviceNodeImpl::fLink>
	> ChildList;
	typedef DoublyLinkedList<
		DeviceNodeImpl, DoublyLinkedListMemberGetLink<DeviceNodeImpl, &DeviceNodeImpl::fPendingLink>
	> PendingList;

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

	BusDriver* fBusDriver {};
	DeviceDriver* fDeviceDriver {};
	ArrayDeleter<char> fDriverModuleName;

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

DeviceManager DeviceManager::sInstance;


// #pragma mark - DeviceNodeImpl


DeviceNodeImpl::DeviceNodeImpl()
{
	dprintf("+DeviceNodeImpl(%p)\n", this);
}


DeviceNodeImpl::~DeviceNodeImpl()
{
	dprintf("-DeviceNodeImpl(%p)\n", this);

	for (int32 i = 0; i < fDevFsNodes.Count(); i++) {
		DevFsNodeWrapper* wrapper = fDevFsNodes[i];
		devfs_unpublish_device(wrapper, true);
		delete wrapper;
	}

	if (fBusDriver != NULL)
		fBusDriver->Free();
}


DeviceNode*
DeviceNodeImpl::GetParent() const
{
	if (fParent != NULL)
		fParent->AcquireReference();

	return fParent;
}


status_t
DeviceNodeImpl::GetNextChildNode(const device_attr* attrs, DeviceNode** node) const
{
	// TODO: implement
	panic("DeviceNodeImpl::GetNextChildNode: not implemented");
	return ENOSYS;
}

status_t
DeviceNodeImpl::FindChildNode(const device_attr* attrs, DeviceNode** node) const
{
	// TODO: implement
	panic("DeviceNodeImpl::FindChildNode: not implemented");
	return ENOSYS;
}


status_t
DeviceNodeImpl::GetNextAttr(device_attr** attr) const
{
	// TODO: implement
	panic("DeviceNodeImpl::GetNextAttr: not implemented");
	return ENOSYS;
}


status_t
DeviceNodeImpl::FindAttr(const char* name, type_code type, int32 index, const void** value) const
{
	const device_attr* attrs = fBusDriver->Attributes();
	if (attrs == NULL)
		return B_NAME_NOT_FOUND;

	for (; attrs->name != NULL; attrs++) {
		if (strcmp(name, attrs->name) != 0)
			continue;

		if (attrs->type != type)
			continue;

		if (index > 0) {
			index--;
			continue;
		}

		*value = &attrs->value;
		return B_OK;
	}

	return B_NAME_NOT_FOUND;
}


void*
DeviceNodeImpl::QueryBusInterface(const char* ifaceName)
{
	if (fBusDriver == NULL)
		return NULL;

	return fBusDriver->QueryInterface(ifaceName);
}

void*
DeviceNodeImpl::QueryDriverInterface(const char* ifaceName)
{
	if (fDeviceDriver == NULL)
		return NULL;

	return fDeviceDriver->QueryInterface(ifaceName);
}


status_t
DeviceNodeImpl::InstallListener(DeviceNodeListener* listener)
{
	// TODO: implement
	panic("DeviceNodeImpl::InstallListener: not implemented");
	return ENOSYS;
}


status_t
DeviceNodeImpl::UninstallListener(DeviceNodeListener* listener)
{
	// TODO: implement
	panic("DeviceNodeImpl::UninstallListener: not implemented");
	return ENOSYS;
}


status_t
DeviceNodeImpl::RegisterNode(BusDriver* driver, DeviceNode** outNode)
{
	BReference<DeviceNodeImpl> node(new(std::nothrow) DeviceNodeImpl(), true);
	if (!node.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(node->Register(this, driver));

	*outNode = node.Detach();
	// dprintf("DeviceNodeImpl::RegisterNode() -> %p\n", *outNode);
	return B_OK;
}


status_t
DeviceNodeImpl::UnregisterNode(DeviceNode* nodeIface)
{
	DeviceNodeImpl* node = static_cast<DeviceNodeImpl*>(nodeIface);
	// dprintf("DeviceNodeImpl::UnregisterNode(%p)\n", node);

	if (!node->fState.registered)
		return B_ERROR; // TODO: better error code?

	if (node->fParent != this)
		return B_ERROR; // TODO: better error code?

	node->SetProbe(false);
	node->fState.registered = false;
	node->fState.unregistered = true;

	fChildNodes.Remove(node);
	node->fParent = NULL;

	node->fBusDriver->Free();
	node->fBusDriver = NULL;

	return B_OK;
}


status_t
DeviceNodeImpl::RegisterDevFsNode(const char* path, DevFsNode* driver)
{
	dprintf("RegisterDevFsNode(\"%s\")\n", path);

	ObjectDeleter<DevFsNodeWrapper> wrapper(new(std::nothrow) DevFsNodeWrapper(driver));
	if (!wrapper.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(devfs_publish_device(path, wrapper.Get()));
	fDevFsNodes.Add(wrapper.Detach());

	return B_OK;
}


status_t
DeviceNodeImpl::UnregisterDevFsNode(const char* path)
{
	BaseDevice* device {};
	CHECK_RET(devfs_get_device(path, device));
	ScopeExit devicePutter([device] {
		devfs_put_device(device);
	});

	for (int32 i = 0; i < fDevFsNodes.Count(); i++) {
		if (fDevFsNodes[i] == device) {
			DevFsNodeWrapper* wrapper = static_cast<DevFsNodeWrapper*>(device);
			fDevFsNodes.Erase(i);
			devfs_unpublish_device(device, true);
			delete wrapper;
			return B_OK;
		}
	}

	return ENOENT;
}


const char*
DeviceNodeImpl::GetName() const
{
	const char* name {};
	if (FindAttrString(B_DEVICE_PRETTY_NAME, &name) < B_OK)
		name = "(no name)";

	return name;
}


status_t
DeviceNodeImpl::Register(DeviceNodeImpl* parent, BusDriver* driver)
{
	if (driver == NULL)
		return B_BAD_VALUE;

	BusDriverDeleter driverDeleter(driver);

	fBusDriver = driver;
	fParent = parent;

	CHECK_RET(fBusDriver->InitDriver(this));

	if (parent == NULL) {
		DeviceManager::Instance().SetRootNode(this);
	} else {
		AcquireReference();
		parent->fChildNodes.Insert(this);
	}

	uint32 flags {};
	if (FindAttrUint32(B_DEVICE_FLAGS, &flags) < B_OK)
		flags = 0;

	if ((flags & B_FIND_MULTIPLE_CHILDREN) != 0)
		fState.multipleDrivers = true;

	fState.registered = true;
	SetProbe(true);
	driverDeleter.Detach();
	// dprintf("node \"%s\" registered\n", GetName());

	return B_OK;
}


status_t
DeviceNodeImpl::Probe()
{
	// dprintf("%p.DeviceNodeImpl::Probe()\n", this);

	if (fState.unregistered)
		panic("DeviceNodeImpl::Probe() called on unregisteded node");

	SetProbe(false);
	fState.probed = true;

	DriverRoster::LookupResultArray candidates;
	DriverRoster::Instance().Lookup(this, candidates);

	for (int32 i = 0; i < candidates.Count(); i++) {
		const char* candidate = candidates[i].module;
		status_t res = ProbeDriver(candidate);
		if (res >= B_OK)
			return B_OK;
	}

	return B_OK;
}


status_t
DeviceNodeImpl::ProbeDriver(const char* moduleName, bool isChild)
{
	// dprintf("%p.DeviceNodeImpl::ProbeDriver(\"%s\", %d)\n", this, moduleName, isChild);
	// dprintf("  fState.multipleDrivers: %d\n", fState.multipleDrivers);

	// Allocate memory first to not fail on no memory when driver already initialized.
	ArrayDeleter<char> driverModuleName(new(std::nothrow) char[strlen(moduleName) + 1]);
	if (!driverModuleName.IsSet())
		return B_NO_MEMORY;

	strcpy(&driverModuleName[0], moduleName);

	if (fState.multipleDrivers && !isChild) {
		DeviceNode* childNodeIface;
		CHECK_RET(fBusDriver->CreateChildNode(&childNodeIface));
		DeviceNodeImpl* childNode = static_cast<DeviceNodeImpl*>(childNodeIface);

		DetachableScopeExit childNodeDeleter([this, childNodeIface]() {
			UnregisterNode(childNodeIface);
		});

		CHECK_RET(childNode->ProbeDriver(moduleName, true));

		childNodeDeleter.Detach();
		return B_OK;
	}

	driver_module_info* driverModule {};
	CHECK_RET(get_module(moduleName, (module_info**)&driverModule));

	DeviceDriver* driver {};
	CHECK_RET(driverModule->probe(this, &driver));
	CHECK_RET(driver->RegisterChildDevices());

	fDeviceDriver = driver;
	fDriverModuleName.SetTo(driverModuleName.Detach());

	return B_OK;
}


void
DeviceNodeImpl::SetProbe(bool doProbe)
{
	// dprintf("%p.DeviceNodeImpl::SetProbe(%d)\n", this, doProbe);

	if (doProbe == fState.probePending)
		return;

	fState.probePending = doProbe;
	if (doProbe)
		DeviceManager::Instance().PendingNodes().Insert(this);
	else
		DeviceManager::Instance().PendingNodes().Remove(this);
}


// #pragma mark - DeviceManager

status_t
DeviceManager::Init()
{
	dprintf("\n");
	dprintf("**************************************\n");
	dprintf("*                                    *\n");
	dprintf("*  Welcome to The Device Manager v2  *\n");
	dprintf("*                                    *\n");
	dprintf("**************************************\n");
	dprintf("\n");

	BReference<DeviceNodeImpl> rootNode(new(std::nothrow) DeviceNodeImpl(), true);
	if (!rootNode.IsSet())
		return B_NO_MEMORY;

	BusDriverDeleter rootBusDriver(new(std::nothrow) RootDevice());
	if (!rootBusDriver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(rootNode->Register(NULL, rootBusDriver.Detach()));

	ProcessPendingNodes();

	DeviceManager::DumpTree();

	return B_OK;
}


status_t
DeviceManager::FileSystemMounted()
{
	dprintf("DeviceManager::FileSystemMounted()\n");
	return B_OK;
}


DeviceNode*
DeviceManager::GetRootNode() const
{
	if (fRoot != NULL)
		fRoot->AcquireReference();

	return fRoot;
}


void
DeviceManager::SetRootNode(DeviceNodeImpl* node)
{
	if (fRoot != NULL)
		panic("root node is already set");

	node->AcquireReference();
	fRoot = node;
}


status_t
DeviceManager::ProcessPendingNodes()
{
	while (!fPendingList.IsEmpty()) {
		DeviceNodeImpl* node = fPendingList.First();
		node->Probe();
	}

	return B_OK;
}


void
DeviceManager::DumpTree()
{
	dprintf("Node tree:\n");
	DumpNode(fRoot, 0);
	dprintf("\n");
}


void
DeviceManager::DumpNode(DeviceNodeImpl* node, int32 level)
{
	auto Indent = [](int32 indent) {
		for (int32 i = 0; i < indent; i++)
			dprintf("  ");
	};

	if (node == NULL)
		return;

	Indent(level);
	const char* name = node->GetName();
	dprintf("Node(\"%s\"): %s\n", name, node->fDeviceDriver == NULL ? "no driver" : &node->fDriverModuleName[0]);

	DeviceNodeImpl* childNode = node->ChildNodes().First();
	for (; childNode != NULL; childNode = node->ChildNodes().GetNext(childNode)) {
		DumpNode(childNode, level + 1);
	}
}


// #pragma mark -

static status_t
device_manager_std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT: {
			new((void*)&DeviceManager::Instance()) DeviceManager();
			DetachableScopeExit deviceManagerDeleter([] {
				DeviceManager::Instance().~DeviceManager();
			});
			CHECK_RET(DeviceManager::Instance().Init());

			new((void*)&DriverRoster::Instance()) DriverRoster();
			DetachableScopeExit driverRosterDeleter([] {
				DriverRoster::Instance().~DriverRoster();
			});
			CHECK_RET(DriverRoster::Instance().Init());

			deviceManagerDeleter.Detach();
			driverRosterDeleter.Detach();
			return B_OK;
		}
		case B_MODULE_UNINIT: {
			DriverRoster::Instance().~DriverRoster();
			DeviceManager::Instance().~DeviceManager();
			return B_OK;
		}

		default:
			return B_ERROR;
	}
}


static device_manager_info sDeviceManagerModule = {
	.info = {
		.name = B_DEVICE_MANAGER_MODULE_NAME,
		.std_ops = device_manager_std_ops,
	},
	.file_system_mounted = []() {return DeviceManager::Instance().FileSystemMounted();},
	.get_root_node = []() {return DeviceManager::Instance().GetRootNode();},
};


_EXPORT module_info* modules[] = {
	(module_info* )&sDeviceManagerModule,
	NULL
};
