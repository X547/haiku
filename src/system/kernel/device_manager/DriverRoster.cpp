#include "DriverRoster.h"

#include <PCI.h>

#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <new>

#include <AutoDeleterPosix.h>

extern struct device_manager_info gDeviceManagerModule;


DriverRoster DriverRoster::sInstance;


void
DriverRoster::DriverWatcher::EventOccurred(NotificationService& service, const KMessage* event)
{
	dprintf("DriverWatcher::EventOccurred\n");
	event->Dump(dprintf);

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


DriverRoster::DirectoryWatcher::~DirectoryWatcher()
{
}


void
DriverRoster::AddDirectoryWatchers(int dirFd)
{
	struct stat stat;
	if (::fstat(dirFd, &stat) < 0)
		return;

	if (!S_ISDIR(stat.st_mode))
		return;

	DirCloser dir(fdopendir(dirFd));
	if (!dir.IsSet())
		return;

	ObjectDeleter<DirectoryWatcher> watcher(new(std::nothrow) DirectoryWatcher({.dev = stat.st_dev, .inode = stat.st_ino}));
	if (!watcher.IsSet())
		return;

	fDirectoryWatchers.Insert(watcher.Detach());
	add_node_listener(stat.st_dev, stat.st_ino, B_WATCH_ALL, fDriverWatcher);

	for (struct dirent* dirent = readdir(dir.Get()); dirent != NULL; dirent = readdir(dir.Get())) {
		if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
			continue;

		dprintf("AddDirectoryWatchers(\"%s\")\n", dirent->d_name);
		FileDescriptorCloser childFd(openat(dirFd, dirent->d_name, O_DIRECTORY | O_RDONLY));
		if (!childFd.IsSet())
			continue;

		// TODO: avoid recursion
		AddDirectoryWatchers(childFd.Get());
	}
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
		struct stat stat;
		if (::stat(paths[i], &stat) >= 0) {
			add_node_listener(stat.st_dev, stat.st_ino, B_WATCH_ALL, fDriverWatcher);
			dprintf("DriverRoster: added node listener for \"%s\"\n", paths[i]);

			FileDescriptorCloser dirFd(open(paths[i], O_DIRECTORY | O_RDONLY));
			if (!dirFd.IsSet())
				continue;

			AddDirectoryWatchers(dirFd.Get());
		}
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
