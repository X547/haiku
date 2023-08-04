#include "DeviceManager.h"

#include <string.h>

#include <ScopeExit.h>

#include "devfs_private.h"

#include "DriverRoster.h"
#include "RootDevice.h"
#include "DevFsNodeWrapper.h"


// TODO: locking
// TODO: check ownership management


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

	UnsetDeviceDriver();

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
DeviceNodeImpl::FindAttr(const char* name, type_code type, int32 index, const void** value, size_t* size) const
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
	// dprintf("DeviceNodeImpl::RegisterNode() -> %p\n", node.Get());

	if (outNode != NULL)
		*outNode = node.Detach();

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
	if (driver == NULL) {
		panic("BusDriver passed to RegisterNode can't be NULL");
		return B_BAD_VALUE;
	}

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

	DriverRoster::Instance().RegisterDeviceNode(this);

	return B_OK;
}


status_t
DeviceNodeImpl::Probe()
{
	dprintf("%p.DeviceNodeImpl::Probe(\"%s\")\n", this, GetName());

	if (fState.unregistered) {
		panic("DeviceNodeImpl::Probe() called on unregistered node");
		return B_ERROR;
	}

	SetProbe(false);
	fState.probed = true;

	for (int32 i = 0; i < fCompatDriverModules.Count(); i++) {
		const char* candidate = fCompatDriverModules.ModuleNameAt(i);
		status_t res = ProbeDriver(candidate);
		if (res >= B_OK && !fState.multipleDrivers)
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
	CHECK_RET(get_module(moduleName, (module_info**)&driverModule));
	DetachableScopeExit modulePutter([moduleName]() {
		put_module(moduleName);
	});

	DeviceDriver* driver {};
	CHECK_RET(driverModule->probe(this, &driver));
	if (driver == NULL) {
		panic("driver_module_info::probe successed, but returned NULL DeviceDriver");
		return B_ERROR;
	}

	modulePutter.Detach();
	fDeviceDriver = driver;
	fDriverModuleName.SetTo(driverModuleName.Detach());

	return B_OK;
}


void
DeviceNodeImpl::UnsetDeviceDriver()
{
	if (fDeviceDriver != NULL) {
		fDeviceDriver->Free();
		fDeviceDriver = NULL;
		put_module(fDriverModuleName.Get());
		fDriverModuleName.Unset();
	}
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

	CHECK_RET(DriverRoster::Instance().Init());

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
	if (fRoot != NULL) {
		panic("root node is already set");
		return;
	}

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

			deviceManagerDeleter.Detach();
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
