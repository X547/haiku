#pragma once

#include <device_manager.h>

#include <Notifications.h>
#include <fs/node_monitor.h>
#include <NodeMonitor.h>

#include <Vector.h>
#include <AVLTree.h>


template <class T, class M>
static inline constexpr ptrdiff_t OffsetOf(const M T::*member)
{
	return reinterpret_cast<ptrdiff_t>(&(reinterpret_cast<T*>(0)->*member));
}

template <class T, class M>
static inline constexpr T& ContainerOf(M &ptr, const M T::*member)
{
	return *reinterpret_cast<T*>(reinterpret_cast<intptr_t>(&ptr) - OffsetOf(member));
}


class DriverRoster {
private:
	class DriverWatcher: public NotificationListener {
	public:
		virtual ~DriverWatcher() = default;
		void EventOccurred(NotificationService& service, const KMessage* event) final;
	};

	class DirectoryWatcher {
	public:
		struct Key {
			dev_t dev;
			ino_t inode;
			char name[MAXPATHLEN];
		};

		struct NodeDef {
			typedef ::DriverRoster::DirectoryWatcher::Key Key;
			typedef DirectoryWatcher Value;

			inline AVLTreeNode* GetAVLTreeNode(Value* value) const
			{
				return &value->fTreeNode;
			}

			inline Value* GetValue(AVLTreeNode* node) const
			{
				return &ContainerOf(*node, &DirectoryWatcher::fTreeNode);
			}

			inline int Compare(const Key& a, const Value* b) const
			{
				if (a.dev < b->fKey.dev) return -1;
				if (a.dev > b->fKey.dev) return 1;
				if (a.inode < b->fKey.inode) return -1;
				if (a.inode > b->fKey.inode) return 1;
				return strcmp(a.name, b->fKey.name);
			}

			inline int Compare(const Value* a, const Value* b) const
			{
				return Compare(a->fKey, b);
			}
		};

	private:
		Key fKey;
		AVLTreeNode fTreeNode;

	public:
		DirectoryWatcher(const Key& key): fKey(key) {}
		~DirectoryWatcher();
	};

	static DriverRoster sInstance;

	mutex fLock = MUTEX_INITIALIZER("DriverRoster");
	DriverWatcher fDriverWatcher;
	AVLTree<DirectoryWatcher::NodeDef> fDirectoryWatchers;

	void AddDirectoryWatchers(int dirFd);

public:
	struct LookupResult {
		float score;
		const char* module;
	};
	typedef Vector<LookupResult> LookupResultArray;

	static DriverRoster& Instance() {return sInstance;}

	void InitPostModules();

	void Lookup(device_node* node, LookupResultArray& result);
};
