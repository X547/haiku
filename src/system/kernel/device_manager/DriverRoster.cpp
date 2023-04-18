#include "DriverRoster.h"

#include <PCI.h>

#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <new>

#include <kmodule.h>
#include <syscalls.h>
#include <fs/KPath.h>
#include <vfs.h>

#include <AutoDeleterPosix.h>

extern struct device_manager_info gDeviceManagerModule;


DriverRoster DriverRoster::sInstance;


void
DriverRoster::DriverWatcher::EventOccurred(NotificationService& service, const KMessage* event)
{
	dprintf("DriverWatcher::EventOccurred\n");
	event->Dump(dprintf);

	int32 opcode = event->GetInt32("opcode", -1);
	dev_t device = event->GetInt32("device", -1);
	ino_t directory = event->GetInt64("directory", -1);
	const char* name = event->GetString("name", NULL);

	switch (opcode) {
		case B_ENTRY_CREATED: {
			entry_ref ref {device, directory, name};
			MutexLocker lock(&DriverRoster::Instance().fLock);
			DriverRoster::Instance().AddDirectoryWatchers(ref);
			break;
		}
		case B_ENTRY_REMOVED: {
			entry_ref ref {device, directory, name};
			MutexLocker lock(&DriverRoster::Instance().fLock);
			delete DriverRoster::Instance().fEntryWatchers.Remove(ref);
			break;
		}
	}

	// TODO
#if 0
	MutexLocker lock(DriverRoster::Instance().fLock);

	int32 opcode = event->GetInt32("opcode", -1);
	dev_t device = event->GetInt32("device", -1);
	ino_t directory = event->GetInt64("directory", -1);
	const char* name = event->GetString("name", NULL);

	if (opcode == B_ENTRY_MOVED) {
		// Determine whether it's a move within, out of, or into one
		// of our watched directories.
		ino_t from = event->GetInt64("from directory", -1);
		ino_t to = event->GetInt64("to directory", -1);
		if (sDirectoryNodeHash.Lookup(&from) == NULL) {
			directory = to;
			opcode = B_ENTRY_CREATED;
		} else if (sDirectoryNodeHash.Lookup(&to) == NULL) {
			directory = from;
			opcode = B_ENTRY_REMOVED;
		} else {
			return;
		}
	}

	_kern_open_entry_ref(device, directory, name, );

	DirectoryWatcher::Key key {.dev = };

	switch (opcode) {
		case B_ENTRY_CREATED:
			break;
		case B_ENTRY_REMOVED:
			break;
	}
#endif
}


DriverRoster::EntryWatcher::EntryWatcher(const Key& key):
	fKey({key.device, key.directory, fName})
{
	strcpy(fName, key.name);
}


DriverRoster::DirectoryWatcher::~DirectoryWatcher()
{
	char path[MAXPATHLEN];
	vfs_entry_ref_to_path(fKey.device, fKey.directory, fKey.name, true, path, B_COUNT_OF(path));

	dprintf("-DirectoryWatcher(\"%s\")\n", path);
}


DriverRoster::AddOn::~AddOn()
{
	char path[MAXPATHLEN];
	vfs_entry_ref_to_path(fKey.device, fKey.directory, fKey.name, true, path, B_COUNT_OF(path));

	dprintf("-AddOn(\"%s\")\n", path);

	while (CompatDef* def = fDefs.RemoveHead())
		def->RemoveSelf();
}


void
DriverRoster::AddDirectoryWatchers(const entry_ref& ref)
{
	FileDescriptorCloser dirFd(_kern_open_dir_entry_ref(ref.device, ref.directory, ref.name));

	struct stat stat;
	if (::fstat(dirFd.Get(), &stat) < 0 || !S_ISDIR(stat.st_mode))
		return AddAddOn(ref);

	char path[MAXPATHLEN];
	vfs_entry_ref_to_path(ref.device, ref.directory, ref.name, true, path, B_COUNT_OF(path));
	dprintf("AddDirectoryWatchers(\"%s\")\n", path);

	DirCloser dir(fdopendir(dirFd.Get()));
	if (!dir.IsSet())
		return;

	ObjectDeleter<DirectoryWatcher> watcher(new(std::nothrow) DirectoryWatcher(ref));
	if (!watcher.IsSet())
		return;

	fEntryWatchers.Insert(watcher.Detach());
	if (add_node_listener(stat.st_dev, stat.st_ino, B_WATCH_ALL, fDriverWatcher) < B_OK)
		return;

	for (struct dirent* dirent = readdir(dir.Get()); dirent != NULL; dirent = readdir(dir.Get())) {
		if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
			continue;

		// TODO: avoid recursion
		AddDirectoryWatchers(entry_ref{stat.st_dev, stat.st_ino, dirent->d_name});
	}
}


void
DriverRoster::AddAddOn(const entry_ref& ref)
{
	char path[MAXPATHLEN];
	vfs_entry_ref_to_path(ref.device, ref.directory, ref.name, true, path, B_COUNT_OF(path));
	dprintf("AddAddOn(\"%s\")\n", path);

	ObjectDeleter<AddOn> watcher(new(std::nothrow) AddOn(ref));
	if (!watcher.IsSet())
		return;

	FileDescriptorCloser fd(_kern_open_entry_ref(ref.device, ref.directory, ref.name, O_RDONLY, 0));
	if (fd.IsSet()) {
		dprintf("  fd.IsSet()\n");
		FileDescriptorCloser attrFd(_kern_open_attr(fd.Get(), NULL, "driver", B_RAW_TYPE, O_RDONLY));
		if (attrFd.IsSet()) {
			dprintf("  attrFd.IsSet()\n");
			struct stat stat;
			if (fstat(attrFd.Get(), &stat) >= B_OK) {
				dprintf("  size: %" B_PRIu64 "\n", stat.st_size);
				// TODO: out of memory check
				MemoryDeleter buffer(malloc(stat.st_size));
				// TODO: check read failure
				read_pos(attrFd.Get(), 0, buffer.Get(), stat.st_size);
				KMessage msg;
				msg.SetTo(buffer.Detach(), stat.st_size, 0, KMessage::KMESSAGE_INIT_FROM_BUFFER | KMessage::KMESSAGE_OWNS_BUFFER);
				msg.Dump(dprintf);
				fRootDef.Insert(msg, watcher.Get());
			}
		}
	}

	fEntryWatchers.Insert(watcher.Detach());
}


DriverRoster::CompatDef::CompatDef(CompatDef* parent, const KMessage& msg):
	fParent(parent)
{
	const char* module;
	if (msg.FindString("module", &module) >= B_OK)
		// TODO: check out of memory
		fModule.SetTo(strdup(module));

	const void* data;
	int32 size;
	if (msg.FindData("score", B_FLOAT_TYPE, &data, &size) >= B_OK && size == sizeof(float))
		fScore = *(float*)data;

	KMessageField field;
	if (msg.FindField("attrs", B_MESSAGE_TYPE, &field) >= B_OK) {
		data = field.ElementAt(0, &size);
		// TODO: check out of memory
		fAttrs.SetTo((void*)data, size, 0, KMessage::KMESSAGE_INIT_FROM_BUFFER | KMessage::KMESSAGE_CLONE_BUFFER);
	}

	if (msg.FindField("driver", B_MESSAGE_TYPE, &field) >= B_OK) {
		for (int32 i = 0; i < field.CountElements(); i++) {
			int32 size;
			const void *data = field.ElementAt(i, &size);
			KMessage subMsg;
			subMsg.SetTo((void*)data, size);
			// TODO: check status code
			Insert(subMsg);
		}
	}
}


DriverRoster::CompatDef::~CompatDef()
{
	while (CompatDef* def = fSub.RemoveHead())
		delete def;
}


status_t
DriverRoster::CompatDef::Insert(const KMessage &msg, AddOn* addOn)
{
	// TODO: check out of memory
	ObjectDeleter subDef(new (std::nothrow) CompatDef(this, msg));

	if (addOn != NULL)
		addOn->fDefs.Insert(subDef.Get());

	fSub.Insert(subDef.Detach());

	return B_OK;
}


void
DriverRoster::CompatDef::RemoveSelf()
{
	fParent->fSub.Remove(this);
	delete this;
}


void
DriverRoster::CompatDef::Lookup(Vector<CompatDef*>& matches, device_attr* devAttr)
{
	if (fSub.IsEmpty()) {

	} else {
		for (CompatDef* def = fSub.First(); def != NULL; def = fSub.GetNext(def)) {
			Lookup(matches, devAttr);
		}
	}
}


void
DriverRoster::Init()
{
}


void
DriverRoster::InitPostModules()
{
	// TODO: use find_directory
	const char* paths[] = {
		"/boot/system/add-ons/kernel",
		"/boot/system/non-packaged/add-ons/kernel",
		"/boot/home/config/add-ons/kernel",
		"/boot/home/config/non-packaged/add-ons/kernel",
	};

	for (size_t i = 0; i < B_COUNT_OF(paths); i++) {
		KPath path(paths[i]);
		KPath parentPath = path;
		parentPath.RemoveLeaf();
		struct stat stat;
		if (::stat(parentPath.Path(), &stat) < B_OK)
			continue;

		entry_ref ref{stat.st_dev, stat.st_ino, path.Leaf()};
		MutexLocker lock(&fLock);
		AddDirectoryWatchers(ref);
	}
}


void
DriverRoster::Lookup(device_node* node, LookupResultArray& result)
{
	// TODO: implement compatible hardware information from driver add-on resources
	result.MakeEmpty();
	const char* bus;
	if (gDeviceManagerModule.get_attr_string(node, B_DEVICE_BUS, &bus, false) >= B_OK) {
		if (strcmp(bus, "fdt") == 0) {
			const char* compatible;
			if (gDeviceManagerModule.get_attr_string(node, "fdt/compatible", &compatible, false) >= B_OK) {
				if (strcmp(compatible, "riscv,plic0") == 0
					|| strcmp(compatible, "sifive,fu540-c000-plic") == 0
					|| strcmp(compatible, "sifive,plic-1.0.0") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "interrupt_controllers/plic/driver_v1"});
				} else if (strcmp(compatible, "pci-host-ecam-generic") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "busses/pci/ecam/driver_v1"});
				} else if (strcmp(compatible, "google,goldfish-rtc") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "rtc/goldfish/driver_v1"});
				} else if (strcmp(compatible, "syscon-poweroff") == 0
					|| strcmp(compatible, "syscon-reboot") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "power/syscon/driver_v1"});
				} else if (strcmp(compatible, "opencores,i2c-ocores") == 0
					|| strcmp(compatible, "sifive,fu740-c000-i2c") == 0
					|| strcmp(compatible, "sifive,i2c0") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "busses/i2c/ocores_i2c/driver_v1"});
				} else if (strcmp(compatible, "hid-over-i2c") == 0) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/input/i2c_hid/driver_v1"});
				}
			}
		} else if (strcmp(bus, "pci") == 0) {
			uint16 baseClass, subClass;
			if (gDeviceManagerModule.get_attr_uint16(node, B_DEVICE_TYPE, &baseClass, false) >= B_OK
				&& gDeviceManagerModule.get_attr_uint16(node, B_DEVICE_SUB_TYPE, &subClass, false) >= B_OK) {
				if (baseClass == PCI_mass_storage && subClass == PCI_nvm) {
					result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/disk/nvme_disk/driver_v1"});
				}
			}
		} else if (strcmp(bus, "root") == 0) {
			result.PushBack(LookupResult{.score = 1.0f, .module = "bus_managers/fdt/root/driver_v1"});
			result.PushBack(LookupResult{.score = 1.0f, .module = "bus_managers/random/driver_v1"});
		} else if (strcmp(bus, "generic") == 0) {
			result.PushBack(LookupResult{.score = 1.0f, .module = "drivers/disk/virtual/ram_disk/driver_v1"});
		}
	}
}
