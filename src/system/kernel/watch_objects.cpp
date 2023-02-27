/*
 * Copyright 2022, Haiku. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"

#include "wait_for_objects.h"

#include <AutoDeleter.h>
#include <util/AutoLock.h>
#include <util/AVLTree.h>
#include <util/DoublyLinkedList.h>
#include <util/KMessage.h>

#include <port.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


//namespace {

struct selectsync_impl : public select_info {
	enum Mode {
		semMode,
		portMode,
	};

	uint64 GetKey() const
	{
		return (uint32)object + (static_cast<uint64>(type) << 32);
	}

	struct NodeDef {
		typedef uint64 Key;
		typedef selectsync_impl Value;

		inline AVLTreeNode* GetAVLTreeNode(Value* value) const
		{
			return &value->link;
		}

		inline Value* GetValue(AVLTreeNode* node) const
		{
			return (Value*)((char*)node - offsetof(selectsync_impl, link));
		}

		inline int Compare(const Key& a, const Value* b) const
		{
			if (a < b->GetKey()) return -1;
			if (a > b->GetKey()) return 1;
			return 0;
		}

		inline int Compare(const Value* a, const Value* b) const
		{
			if (a->GetKey() < b->GetKey()) return -1;
			if (a->GetKey() > b->GetKey()) return 1;
			return 0;
		}
	};

	selectsync_impl();
	bool RemoveIfUnneeded();


	struct ReadCallback : public PortReadCallback {
		selectsync_impl *fBase;
		uint16 fEvents;

		ReadCallback(selectsync_impl *base): fBase(base) {}

		selectsync_impl &Base();
		void Do(BReferenceable *port) final;
	};

	struct WriteCallback : public PortWriteCallback {
		selectsync_impl &Base() {return *(selectsync_impl*)((char*)this - offsetof(selectsync_impl, fWriteCallback));}
		bool Do(BReferenceable *port) final;
	} fWriteCallback;

	AVLTreeNode			link;
	int32				object;
	uint16				type;
	uint16				enqueued_events = 0;
};

struct selectsync_group_impl : public select_sync {
	mutex				lock;

	port_id				port;
	int32				token;
	bool				is_message;

	AVLTree<selectsync_impl::NodeDef> set;

	selectsync_group_impl();
};

//}


bool selectsync_impl::RemoveIfUnneeded()
{
	if (selected_events == 0
		&& events == 0
		&& enqueued_events == 0) {
		static_cast<selectsync_group_impl*>(sync)->set.Remove(this);
		delete this;
		return true;
	}
	return false;
}


void selectsync_impl::ReadCallback::Do(BReferenceable *port)
{
	selectsync_impl *sync = fBase;
	selectsync_group_impl *group = static_cast<selectsync_group_impl*>(sync->sync);
	(void)group;
	// TODO: locking

	sync->enqueued_events &= ~fEvents;
	if (sync->RemoveIfUnneeded())
		return;

	object_wait_info info {
		.object = sync->object,
		.type = sync->type,
		.events = sync->selected_events,
	};
	deselect_object(&info, sync, true);
	select_object(&info, sync, true);
}


bool selectsync_impl::WriteCallback::Do(BReferenceable *port)
{
	KMessage msg;
	// TODO: fill KMessage

	ObjectDeleter<ReadCallback> readCallback(new ReadCallback(&Base()));

	if (Write(port, 'KMSG', msg.Buffer(), msg.ContentSize(), readCallback.Get()) < B_OK)
		return true;

	readCallback.Detach();
	return false;
}


static select_sync*
new_selectsync_group_impl()
{
	return new(std::nothrow) selectsync_group_impl();
}


static void
put_selectsync_group_impl(select_sync* _group)
{
	selectsync_group_impl *group = static_cast<selectsync_group_impl*>(_group);

	for (;;) {
		selectsync_impl *sync = group->set.LeftMost();
		if (sync == NULL) break;
		group->set.Remove(sync);
		delete sync;
	}

	delete group;
}


static status_t
notify_select_events_impl(select_info* _sync, uint16 events)
{
	selectsync_impl *sync = static_cast<selectsync_impl*>(_sync);
	selectsync_group_impl *group = static_cast<selectsync_group_impl*>(sync->sync);
	(void)group;

	events &= ~sync->enqueued_events;
	if (events == 0)
		return B_OK;

	if (sync->events == 0) {
		sync->events = events;
		add_port_write_callback(group->port, &sync->fWriteCallback);
	} else
		sync->events |= events;

	return B_OK;
}


selectsync_group_impl::selectsync_group_impl()
{
	put = put_selectsync_group_impl;
	ref_count = 1;
	port = -1;
}


selectsync_impl::selectsync_impl()
{
	notify = notify_select_events_impl;
	next = NULL;
	selected_events = 0;
	events = 0;
}


status_t
watch_objects_int(selectsync_group_impl *group, object_wait_info* infos, int numInfos, uint32 flags, bool kernel)
{
	MutexLocker lock(&group->lock);
	object_wait_info* infosEnd = infos + numInfos;
	for (object_wait_info *info = infos; infos != infosEnd; info++) {
		selectsync_impl *sync = group == NULL ? NULL : group->set.Find((uint32)info->object + (static_cast<uint64>(info->type) << 32));
		if (sync == NULL) {
			if (info->events != 0) {
				sync = new(std::nothrow) selectsync_impl();
				sync->sync = group;
				sync->object = info->object;
				sync->type = info->type;
				sync->selected_events = info->events;
				select_object(info, sync, kernel);
				group->set.Insert(sync);
			}
		} else {
			deselect_object(info, sync, kernel);
			sync->selected_events = info->events;
			sync->events &= ~(uint32)info->events;
			sync->RemoveIfUnneeded();
			if (info->events != 0)
				select_object(info, sync, kernel);
		}
	}
	return B_OK;
}


status_t
watch_objects(port_id port, int32 token, object_wait_info* infos, int numInfos, uint32 flags)
{
	selectsync_group_impl *group;
	CHECK_RET(port_get_selectsync_group(port, (select_sync**)&group, new_selectsync_group_impl));
	if (group->port < 0) {
		group->port = port;
		group->token = token;
		group->is_message = false;
	}
	CHECK_RET(watch_objects_int(group, infos, numInfos, flags, true));
	return B_OK;
}
