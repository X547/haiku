/*
 * Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#pragma once

#include "BaseDevice.h"

#include <atomic>

#include <Referenceable.h>

#include <dm2/device_manager.h>

#include <util/DoublyLinkedList.h>


class DevFsNodeWrapper : public BaseDevice, public BReferenceable {
public:
							DevFsNodeWrapper(DevFsNode* devFsNode);
	virtual					~DevFsNodeWrapper() {dprintf("-%p.DevFsNodeWrapper()\n", this);}

	DevFsNode*				GetDevFsNode() {return fDevFsNode;}

	virtual	status_t		InitDevice();
			void			UninitDevice();

	void					Finalize();

	virtual	bool			HasSelect() const;
	virtual	bool			HasDeselect() const;
	virtual	bool			HasRead() const;
	virtual	bool			HasWrite() const;
	virtual	bool			HasIO() const;

	virtual	status_t		Open(const char* path, int openMode,
								void** _cookie);
	virtual	status_t		Read(void* cookie, off_t pos, void* buffer,
								size_t* _length);
	virtual	status_t		Write(void* cookie, off_t pos, const void* buffer,
								size_t* _length);
	virtual	status_t		IO(void* cookie, io_request* request);
	virtual	status_t		Control(void* cookie, int32 op, void* buffer,
								size_t length);
	virtual	status_t		Select(void* cookie, uint8 event, selectsync* sync);
	virtual	status_t		Deselect(void* cookie, uint8 event,
								selectsync* sync);

	virtual	status_t		Close(void* cookie);
	virtual	status_t		Free(void* cookie);

protected:
			status_t 		_DoIO(void* cookie, off_t pos,
								void* buffer, size_t* _length, bool isWrite);

private:
	DoublyLinkedListLink<DevFsNodeWrapper> fLink;

public:
	typedef DoublyLinkedList<
		DevFsNodeWrapper, DoublyLinkedListMemberGetLink<DevFsNodeWrapper, &DevFsNodeWrapper::fLink>
	> List;

protected:
	DevFsNode*				fDevFsNode;
	DevFsNode::Capabilities	fCapabilities;
	std::atomic<bool>		fIsFinalized {false};
	int32					fOpenCount {};
};
