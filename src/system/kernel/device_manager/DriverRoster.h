#pragma once

#include <optional>

#include <device_manager.h>

#include <Notifications.h>
#include <fs/node_monitor.h>
#include <NodeMonitor.h>

#include <Vector.h>
#include <AVLTree.h>
#include <DoublyLinkedList.h>
#include <AutoDeleter.h>


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


struct entry_ref {
	dev_t device;
	ino_t directory;
	const char* name;
};


class DriverRoster {
private:
	class DriverWatcher: public NotificationListener {
	public:
		virtual ~DriverWatcher() = default;
		void EventOccurred(NotificationService& service, const KMessage* event) final;
	};

	class EntryWatcher {
	public:
		typedef entry_ref Key;

	private:
		struct NodeDef {
			typedef ::DriverRoster::EntryWatcher::Key Key;
			typedef EntryWatcher Value;

			inline AVLTreeNode* GetAVLTreeNode(Value* value) const
			{
				return &value->fTreeNode;
			}

			inline Value* GetValue(AVLTreeNode* node) const
			{
				return &ContainerOf(*node, &EntryWatcher::fTreeNode);
			}

			inline int Compare(const Key& a, const Value* b) const
			{
				if (a.device < b->fKey.device) return -1;
				if (a.device > b->fKey.device) return 1;
				if (a.directory < b->fKey.directory) return -1;
				if (a.directory > b->fKey.directory) return 1;
				return strcmp(a.name, b->fKey.name);
			}

			inline int Compare(const Value* a, const Value* b) const
			{
				return Compare(a->fKey, b);
			}
		};

	protected:
		Key fKey;
		char fName[MAXPATHLEN];
		AVLTreeNode fTreeNode;

	public:
		typedef AVLTree<NodeDef> Map;

		EntryWatcher(const Key& key);
		virtual ~EntryWatcher() = default;
	};

	class DirectoryWatcher: public EntryWatcher {
	public:
		DirectoryWatcher(const Key& key): EntryWatcher(key) {}
		virtual ~DirectoryWatcher();
	};

	class AddOn;

	class CompatDef {
	private:
		DoublyLinkedListLink<CompatDef> fSubLink;
		DoublyLinkedListLink<CompatDef> fAddOnLink;

	public:
		typedef DoublyLinkedList<CompatDef, DoublyLinkedListMemberGetLink<CompatDef, &CompatDef::fSubLink>> SubList;
		typedef DoublyLinkedList<CompatDef, DoublyLinkedListMemberGetLink<CompatDef, &CompatDef::fAddOnLink>> AddOnList;

	private:
		CompatDef* fParent {};
		MemoryDeleter fModule;
		std::optional<float> fScore;
		KMessage fAttrs;
		SubList fSub;

	public:
		CompatDef() {};
		CompatDef(CompatDef* parent, const KMessage &msg);
		~CompatDef();

		status_t Insert(const KMessage &msg, AddOn* addOn = NULL);
		void RemoveSelf();

		void Lookup(Vector<CompatDef*>& matches, device_attr* devAttr);
	};

	class AddOn: public EntryWatcher {
	private:
		friend class CompatDef;

		CompatDef::AddOnList fDefs;

	public:
		AddOn(const Key& key): EntryWatcher(key) {}
		virtual ~AddOn();
	};

	static DriverRoster sInstance;

	mutex fLock = MUTEX_INITIALIZER("DriverRoster");
	DriverWatcher fDriverWatcher;
	EntryWatcher::Map fEntryWatchers;
	CompatDef fRootDef;

	void AddDirectoryWatchers(const entry_ref& ref);
	void AddAddOn(const entry_ref& ref);

public:
	struct LookupResult {
		float score;
		const char* module;
	};
	typedef Vector<LookupResult> LookupResultArray;

	static DriverRoster& Instance() {return sInstance;}

	void Init();
	void InitPostModules();

	void Lookup(device_node* node, LookupResultArray& result);
};
