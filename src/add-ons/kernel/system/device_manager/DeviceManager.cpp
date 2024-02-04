#include "DeviceManager.h"

#include <string.h>

#include <ScopeExit.h>

#include <debug.h>

#include "devfs_private.h"

#include "DriverRoster.h"
#include "RootDevice.h"
#include "DevFsNodeWrapper.h"
#include "UserlandInterface.h"
#include "UserlandInterface2Private.h"


// TODO: locking
// TODO: check ownership management


DeviceManager DeviceManager::sInstance;


class UnlockLocking {
public:
	inline bool Lock(recursive_lock *lockable)
	{
		recursive_lock_lock(lockable);
		fRecursion = lockable->recursion - 1;
		//dprintf("UnlockLocking::Lock(), recursion: %d\n", fRecursion);
		if (fRecursion <= 0) {
			panic("UnlockLocking::Lock(): not locked\n");
			recursive_lock_unlock(lockable);
			return false;
		}
		lockable->recursion = 1;
		recursive_lock_unlock(lockable);
		return true;
	}

	inline void Unlock(recursive_lock *lockable)
	{
		recursive_lock_lock(lockable);
		//dprintf("UnlockLocking::Unlock(), recursion: %d\n", fRecursion);
		lockable->recursion = fRecursion;
	}

private:
	int fRecursion = 0;
};

typedef AutoLocker<recursive_lock, UnlockLocking> UnlockLocker;


static status_t
copy_attributes(const device_attr* attrs, ArrayDeleter<device_attr> &outAttrs, ArrayDeleter<uint8> &outAttrData)
{
	size_t attrDataSize = 0;
	size_t attrCount = 0;
	for (const device_attr* attr = attrs; attr->name != NULL; attr++) {
		switch (attr->type) {
			case B_UINT8_TYPE:
			case B_UINT16_TYPE:
			case B_UINT32_TYPE:
			case B_UINT64_TYPE:
				break;
			case B_STRING_TYPE:
				attrDataSize += strlen(attr->value.string) + 1;
				break;
			case B_RAW_TYPE:
				attrDataSize += attr->value.raw.length;
				break;
			default:
				return B_BAD_VALUE;
		}
		attrCount++;
	}
	outAttrs.SetTo(new(std::nothrow) device_attr[attrCount + 1]);
	if (!outAttrs.IsSet())
		return B_NO_MEMORY;

	outAttrData.SetTo(new(std::nothrow) uint8[attrDataSize]);
	if (!outAttrData.IsSet())
		return B_NO_MEMORY;

	uint8* attrData = &outAttrData[0];

	for (const device_attr* attr = attrs; attr->name != NULL; attr++) {
		device_attr& outAttr = outAttrs[attr - attrs];
		outAttr = *attr;

		switch (attr->type) {
			case B_STRING_TYPE: {
				size_t curDataSize = strlen(attr->value.string) + 1;
				memcpy(attrData, attr->value.string, curDataSize);
				outAttr.value.string = (const char*)attrData;
				attrData += curDataSize;
				break;
			}
			case B_RAW_TYPE:
				memcpy(attrData, attr->value.raw.data, attr->value.raw.length);
				outAttr.value.raw.data = (const char*)attrData;
				attrData += attr->value.raw.length;
				break;
		}
	}
	memset(&outAttrs[attrCount], 0, sizeof(device_attr));

	return B_OK;
}


// #pragma mark - DeviceNodeImpl

DeviceNodeImpl::DeviceNodeImpl()
{
	dprintf("+DeviceNodeImpl(%p)\n", this);
	fProbeCompletedCond.Init(this, "probeCompletedCond");
}


DeviceNodeImpl::~DeviceNodeImpl()
{
	dprintf("-DeviceNodeImpl(%p, \"%s\")\n", this, GetName());

	UnsetDeviceDriver();

	if (fBusDriver != NULL)
		fBusDriver->Free();
}


// #pragma mark - DeviceNodeImpl public API

DeviceNode*
DeviceNodeImpl::GetParent() const
{
	MutexLocker lock(&fLock);

	if (fParent != NULL)
		fParent->AcquireReference();

	return fParent;
}


status_t
DeviceNodeImpl::GetNextChildNode(const device_attr* attrs, DeviceNode** outNode) const
{
	MutexLocker lock(&fLock);

	// TODO: implement attribute filtering
	if (attrs != NULL)
		return B_BAD_VALUE;

	if (*outNode == NULL) {
		if (fChildNodes.IsEmpty())
			return B_ENTRY_NOT_FOUND;

		DeviceNodeImpl* node = fChildNodes.First();
		node->AcquireReference();
		*outNode = node;
		return B_OK;
	}

	DeviceNodeImpl* node = static_cast<DeviceNodeImpl*>(*outNode);
	DeviceNodeImpl* nextNode = fChildNodes.GetNext(node);
	*outNode = nextNode;
	if (nextNode == NULL)
		return B_ENTRY_NOT_FOUND;

	nextNode->AcquireReference();
	return B_OK;
}


status_t
DeviceNodeImpl::FindChildNode(const device_attr* attrs, DeviceNode** outNode) const
{
	// TODO: implement
	panic("DeviceNodeImpl::FindChildNode: not implemented");
	return ENOSYS;
}


status_t
DeviceNodeImpl::GetNextAttr(const device_attr** attr) const
{
	// Attributes are immutable so no lock is needed.

	const device_attr* attrs = &fAttributes[0];
	if (attrs == NULL)
		return B_ENTRY_NOT_FOUND;

	if (*attr == NULL) {
		if (attrs[0].name == NULL)
			return B_ENTRY_NOT_FOUND;

		*attr = attrs;
		return B_OK;
	}

	if ((*attr)->name == NULL)
		return B_ENTRY_NOT_FOUND;

	(*attr)++;
	if ((*attr)->name == NULL)
		return B_ENTRY_NOT_FOUND;

	return B_OK;
}


status_t
DeviceNodeImpl::FindAttr(const char* name, type_code type, int32 index, const void** value, size_t* size) const
{
	// Attributes are immutable so no lock is needed.

	const device_attr* attrs = &fAttributes[0];
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
		switch (type) {
			case B_UINT8_TYPE:
			case B_UINT16_TYPE:
			case B_UINT32_TYPE:
			case B_UINT64_TYPE:
				*value = &attrs->value;
				break;
			case B_STRING_TYPE:
				*value = attrs->value.string;
				break;
			case B_RAW_TYPE:
				*value = attrs->value.raw.data;
				break;
		}
		if (size != NULL) {
			switch (type) {
				case B_UINT8_TYPE:
					*size = 1;
					break;
				case B_UINT16_TYPE:
					*size = 2;
					break;
				case B_UINT32_TYPE:
					*size = 4;
					break;
				case B_UINT64_TYPE:
					*size = 8;
					break;
				case B_STRING_TYPE:
					*size = strlen(attrs->value.string) + 1;
					break;
				case B_RAW_TYPE:
					*size = attrs->value.raw.length;
					break;
				default:
					*size = 0;
			}
		}
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
DeviceNodeImpl::QueryDriverInterface(const char* ifaceName, DeviceNode* dep)
{
	Probe();

	MutexLocker lock(&fLock);
	DeviceDriver* deviceDriver = fDeviceDriver;
	lock.Unlock();

	if (deviceDriver == NULL)
		return NULL;

	void* iface = deviceDriver->QueryInterface(ifaceName);
	if (iface == NULL)
		return NULL;
#if 0
	if (dep != NULL)
		fDependants.Insert(static_cast<DriverDependencyImpl*>(dep));
#endif
	return iface;
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
DeviceNodeImpl::RegisterNode(DeviceNode* owner, BusDriver* driver, const device_attr* attrs, DeviceNode** outNode)
{
	BReference<DeviceNodeImpl> node(new(std::nothrow) DeviceNodeImpl(), true);
	if (!node.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(node->Register(this, static_cast<DeviceNodeImpl*>(owner), driver, attrs));
	// dprintf("DeviceNodeImpl::RegisterNode() -> %p\n", node.Get());

	if (outNode != NULL)
		*outNode = node.Detach();

	return B_OK;
}


status_t
DeviceNodeImpl::UnregisterNode(DeviceNode* nodeIface)
{
	DeviceNodeImpl* node = static_cast<DeviceNodeImpl*>(nodeIface);
	//dprintf("%p.DeviceNodeImpl::UnregisterNode(\"%s\")\n", node, node->GetName());

	MutexLocker lock(&node->fLock);

	if (!node->fState.registered)
		return B_ERROR; // TODO: better error code?

	if (node->fParent != this)
		return B_ERROR; // TODO: better error code?

	lock.Unlock();
	node->UnsetDeviceDriver();

	node->SetProbePending(false);
	lock.Lock();
	node->fState.registered = false;
	node->fState.unregistered = true;

	fChildNodes.Remove(node);
	node->fParent = NULL;

	node->fCompatDriverModules.Clear();
	lock.Unlock();
	DriverRoster::Instance().UnregisterDeviceNode(node);

	node->fBusDriver->Free();
	node->fBusDriver = NULL;
	node->ReleaseReference();

	return B_OK;
}


status_t
DeviceNodeImpl::RegisterDevFsNode(const char* path, DevFsNode* driver)
{
	dprintf("RegisterDevFsNode(\"%s\")\n", path);
	if (driver == NULL) {
		panic("DevFsNode passed to RegisterDevFsNode can't be NULL");
		return B_BAD_VALUE;
	}

	ObjectDeleter<DevFsNodeWrapper> wrapper(new(std::nothrow) DevFsNodeWrapper(driver));
	if (!wrapper.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(devfs_publish_device(path, wrapper.Get()));
	MutexLocker lock(&fLock);
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

	MutexLocker lock(&fLock);
	DevFsNodeWrapper* wrapper = static_cast<DevFsNodeWrapper*>(device);
	if (!fDevFsNodes.Contains(wrapper))
		return ENOENT;

	fDevFsNodes.Remove(wrapper);
	lock.Unlock();
	devfs_unpublish_device(device, true);
	wrapper->Finalize();
	return B_OK;
}


// #pragma mark - DeviceNodeImpl private API

const char*
DeviceNodeImpl::GetName() const
{
	const char* name {};
	if (FindAttrString(B_DEVICE_PRETTY_NAME, &name) < B_OK)
		name = "(no name)";

	return name;
}


status_t
DeviceNodeImpl::Register(
	DeviceNodeImpl* parent,
	DeviceNodeImpl* owner,
	BusDriver* driver,
	const device_attr* attrs)
{
	if (driver == NULL) {
		panic("BusDriver passed to RegisterNode can't be NULL");
		return B_BAD_VALUE;
	}

	BusDriverDeleter driverDeleter(driver);

	fOwner = owner;
	fBusDriver = driver;
	fParent = parent;

	CHECK_RET(copy_attributes(attrs, fAttributes, fAttrData));

	CHECK_RET(fBusDriver->InitDriver(this));

	if (parent == NULL) {
		DeviceManager::Instance().SetRootNode(this);
	} else {
		AcquireReference();
		MutexLocker lock(&parent->fLock);
		parent->fChildNodes.Insert(this);
	}

	uint32 flags {};
	if (FindAttrUint32(B_DEVICE_FLAGS, &flags) < B_OK)
		flags = 0;

	if ((flags & B_FIND_MULTIPLE_CHILDREN) != 0)
		fState.multipleDrivers = true;

	DriverRoster::Instance().RegisterDeviceNode(this);

	fState.registered = true;
	SetProbePending(true);
	driverDeleter.Detach();
	// dprintf("node \"%s\" registered\n", GetName());

	return B_OK;
}


status_t
DeviceNodeImpl::Probe()
{
	MutexLocker lock(&fLock);
	if (fState.unregistered) {
		panic("DeviceNodeImpl::Probe() called on unregistered node");
		return B_ERROR;
	}

	while (fState.inProbe)
		fProbeCompletedCond.Wait(&fLock);

	fState.inProbe = true;
	ScopeExit scopeExit([this, &lock] {
		lock.Unlock();
		SetProbePending(false);
		lock.Lock();
		fState.inProbe = false;
		fProbeCompletedCond.NotifyAll();
		if (!fState.driverAttached && fDeviceDriver != NULL) {
			fState.driverAttached = true;
			BusDriver* busDriver = fBusDriver;
			DeviceDriver* deviceDriver = fDeviceDriver;
			lock.Unlock();
			busDriver->DriverAttached(true);
			deviceDriver->BusReady();
			lock.Lock();
		}
	});

	if (fState.probed)
		return B_OK;
	fState.probed = true;
	lock.Unlock();

	dprintf("%p.DeviceNodeImpl::Probe(\"%s\")\n", this, GetName());

	const char* fixedChildModule;
	if (FindAttrString(B_DEVICE_FIXED_CHILD, &fixedChildModule) >= B_OK) {
		if (ProbeDriver(fixedChildModule) < B_OK) {
			dprintf("[!] failed to probe driver \"%s\" for node \"%s\"\n", fixedChildModule, GetName());
		}
		return B_OK;
	}

	for (int32 i = 0; i < fCompatDriverModules.Count(); i++) {
		const char* candidate = fCompatDriverModules.ModuleNameAt(i);
		status_t res = ProbeDriver(candidate);
		if (res < B_OK) {
			dprintf("[!] failed to probe driver \"%s\" for node \"%s\"\n", candidate, GetName());
		}
		if (res >= B_OK && !fState.multipleDrivers)
			return B_OK;
	}

	return B_OK;
}


status_t
DeviceNodeImpl::ProbeDriver(const char* moduleName, bool isChild)
{
	dprintf("%p.DeviceNodeImpl::ProbeDriver(\"%s\", %d)\n", this, moduleName, isChild);
	dprintf("  fState.multipleDrivers: %d\n", fState.multipleDrivers);

	// Allocate memory first to not fail on no memory when driver already initialized.
	CStringDeleter driverModuleName(strdup(moduleName));
	if (!driverModuleName.IsSet())
		return B_NO_MEMORY;

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
	CHECK_RET_MSG(get_module(moduleName, (module_info**)&driverModule), "[!] can't load driver module\n");
	DetachableScopeExit modulePutter([moduleName]() {
		put_module(moduleName);
	});

	// TODO: unregister nodes and DevFS nodes on probe fail
	DeviceDriver* driver {};
	CHECK_RET_MSG(driverModule->probe(this, &driver), "[!] driver do not support device or internal driver error\n");
	if (driver == NULL) {
		panic("driver_module_info::probe successed, but returned NULL DeviceDriver");
		return B_ERROR;
	}

	modulePutter.Detach();
	{
		MutexLocker lock(&fLock);
		fDeviceDriver = driver;
		fDriverModuleName.SetTo(driverModuleName.Detach());
	}

	return B_OK;
}


void
DeviceNodeImpl::UnsetDeviceDriver()
{
	MutexLocker lock(&fLock);
	if (fDeviceDriver == NULL)
		return;

	lock.Unlock();
	DeviceManager::Instance().GetRootNodeNoRef()->UnregisterOwnedNodes(this);
	fDeviceDriver->DeviceRemoved();
	lock.Lock();
	dprintf("UnsetDeviceDriver(\"%s\", \"%s\")\n", GetName(), fDriverModuleName.Get());
	while (!fDevFsNodes.IsEmpty()) {
		DevFsNodeWrapper* wrapper = fDevFsNodes.RemoveHead();
		lock.Unlock();
		devfs_unpublish_device(wrapper, true);
		lock.Lock();
		wrapper->Finalize();
	}
	BusDriver* busDriver = fBusDriver;
	DeviceDriver* deviceDriver = fDeviceDriver;

	lock.Unlock();
	busDriver->DriverAttached(false);
	deviceDriver->Free();
	put_module(fDriverModuleName.Get());

	lock.Lock();
	fState.driverAttached = false;
	fDeviceDriver = NULL;
	fDriverModuleName.Unset();
}


void
DeviceNodeImpl::InsertCompatDriverModule(DriverModuleInfo* module, float score)
{
	fCompatDriverModules.Insert(module, score);
}


void
DeviceNodeImpl::RemoveCompatDriverModule(DriverModuleInfo* module)
{
	fCompatDriverModules.Remove(module);
}


void
DeviceNodeImpl::SetProbePending(bool doProbe)
{
	// dprintf("%p.DeviceNodeImpl::SetProbePending(%d)\n", this, doProbe);
	{
		MutexLocker lock(&fLock);
		if (doProbe == fState.probePending)
			return;

		fState.probePending = doProbe;
	}
	DeviceManager::Instance().AddToProbePendingList(this, doProbe);
}


void
DeviceNodeImpl::UnregisterOwnedNodes(DeviceNodeImpl* owner)
{
	MutexLocker lock(&fLock);
	DeviceNodeImpl* node = fChildNodes.First();
	while (node != NULL) {
		DeviceNodeImpl* next = fChildNodes.GetNext(node);
		lock.Unlock();
		// TODO: next may be unregistered here
		node->UnregisterOwnedNodes(owner);
		lock.Lock();
		node = next;
	}
	lock.Unlock();
	if (fOwner == owner)
		fParent->UnregisterNode(this);
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

	CHECK_RET(DriverRoster::Instance().Init());
	fPendingListEmptyCond.Init(this, "pendingListEmptyCond");

	BReference<DeviceNodeImpl> rootNode(new(std::nothrow) DeviceNodeImpl(), true);
	if (!rootNode.IsSet())
		return B_NO_MEMORY;

	BusDriverDeleter rootBusDriver(new(std::nothrow) RootDevice());
	if (!rootBusDriver.IsSet())
		return B_NO_MEMORY;

	static const device_attr rootAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Devices Root"}},
		{B_DEVICE_BUS,         B_STRING_TYPE, {.string = "root"}},
		{B_DEVICE_FLAGS,       B_UINT32_TYPE, {.ui32   = B_FIND_MULTIPLE_CHILDREN}},
		{}
	};
	CHECK_RET(rootNode->Register(NULL, NULL, rootBusDriver.Detach(), rootAttrs));


	static const device_attr deviceManagerAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Device Manager"}},
		{B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {.string = DEVICE_MANAGER_DRIVER_MODULE_NAME}},
		{}
	};

	rootBusDriver.SetTo(new(std::nothrow) RootDevice());
	if (!rootBusDriver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(rootNode->RegisterNode(rootNode.Get(), rootBusDriver.Detach(), deviceManagerAttrs, NULL));

	return B_OK;
}


DeviceNodeImpl*
DeviceManager::GetRootNode() const
{
	if (fRoot != NULL)
		fRoot->AcquireReference();

	return fRoot;
}


void
DeviceManager::SetRootNode(DeviceNodeImpl* node)
{
	MutexLocker lock(&fLock);

	if (fRoot != NULL) {
		panic("root node is already set");
		return;
	}

	node->AcquireReference();
	fRoot = node;
}


void
DeviceManager::AddToProbePendingList(DeviceNodeImpl* node, bool doAdd)
{
	MutexLocker lock(&fLock);
	if (doAdd) {
		fPendingList.Insert(node);
		if (atomic_get(&fProbeLockCount) <= 0)
			fDPCQueue->Add(this);
	} else {
		fPendingList.Remove(node);
		if (fPendingList.IsEmpty())
			fPendingListEmptyCond.NotifyAll();
	}
}


void
DeviceManager::LockProbe()
{
	if (atomic_add(&fProbeLockCount, 1) == 0) {
		fDPCQueue->Cancel(this);
	}
}


void
DeviceManager::UnlockProbe()
{
	if (atomic_add(&fProbeLockCount, -1) == 1) {
		MutexLocker lock(&fLock);
		if (!fPendingList.IsEmpty())
			fDPCQueue->Add(this);
	}
}


status_t
DeviceManager::ProbeFence()
{
	MutexLocker lock(&fLock);
	dprintf("ProbeFence()\n");
	while (!fPendingList.IsEmpty())
		fPendingListEmptyCond.Wait(&fLock);

	return B_OK;
}


void
DeviceManager::DoDPC(DPCQueue* queue)
{
	dprintf("DeviceManager::DoDPC\n");

	MutexLocker lock(&fLock);
	DeviceNodeImpl* node = fPendingList.First();
	if (node == NULL)
		return;

	lock.Unlock();
	node->Probe();
	lock.Lock();

	if (!fPendingList.IsEmpty())
		fDPCQueue->Add(this);
}


void
DeviceManager::DumpTree()
{
	dprintf("Node tree:\n");
	DumpNode(fRoot, 0);
	dprintf("\n");
}


static
DeviceNodeImpl* find_node(DeviceNodeImpl* node, const char* name)
{
	if (strcmp(node->GetName(), name) == 0) {
		node->AcquireReference();
		return node;
	}

	for (
		DeviceNodeImpl *child = node->ChildNodes().First();
		child != NULL;
		child = node->ChildNodes().GetNext(child)
	) {
		DeviceNodeImpl* res = find_node(child, name);
		if (res != NULL)
			return res;
	}

	return NULL;
}


void
DeviceManager::RunTest(const char* testName)
{
	dprintf("DeviceManager::RunTest(\"%s\")\n", testName);

	if (strcmp(testName, "driverDetach1") == 0) {
		BReference<DeviceNodeImpl> root(static_cast<DeviceNodeImpl*>(Instance().GetRootNode()), true);
		BReference<DeviceNodeImpl> node(find_node(root.Get(), "i2c@8"), true);

		node->UnsetDeviceDriver();
		Instance().DumpTree();

		node->fState.probed = false;
		node->SetProbePending(true);
		ProbeFence();
		Instance().DumpTree();

#if 0
		dprintf("node: \"%s\"\n", node->GetName());
#endif
	}
	panic("(!)");
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
	dprintf("Node(\"%s\"): %s\n", name, node->fDeviceDriver == NULL ? "no driver" : node->fDriverModuleName.Get());

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
			new((void*)&DriverRoster::Instance()) DriverRoster();
			DetachableScopeExit deviceManagerDeleter([] {
				DriverRoster::Instance().~DriverRoster();
				DeviceManager::Instance().~DeviceManager();
			});

			CHECK_RET(DeviceManager::Instance().Init());

			device_manager_install_userland_iface();

			deviceManagerDeleter.Detach();
			return B_OK;
		}
		case B_MODULE_UNINIT: {
			device_manager_uninstall_userland_iface();

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
	.get_root_node = []() -> DeviceNode* {return DeviceManager::Instance().GetRootNode();},
	.probe_fence = []() {return DeviceManager::Instance().ProbeFence();},
	.dump_tree = []() {DeviceManager::Instance().DumpTree();},
	.run_test = [](const char* testName) {DeviceManager::Instance().RunTest(testName);},
};


_EXPORT module_info* modules[] = {
	(module_info*)&sDeviceManagerModule,
	(module_info*)&gDeviceManagerDriverModule,
	NULL
};
