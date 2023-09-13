/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */


#include "DevFsNodeWrapper.h"

#include "IORequest.h"


DevFsNodeWrapper::DevFsNodeWrapper(DevFsNode* devFsNode)
	:
	fDevFsNode(devFsNode),
	fCapabilities(devFsNode->GetCapabilities())
{
	dprintf("+%p.DevFsNodeWrapper()\n", this);
}


bool
DevFsNodeWrapper::HasSelect() const
{
	return fCapabilities.select;
}


bool
DevFsNodeWrapper::HasDeselect() const
{
	return fCapabilities.select;
}


bool
DevFsNodeWrapper::HasRead() const
{
	return fCapabilities.read;
}


bool
DevFsNodeWrapper::HasWrite() const
{
	return fCapabilities.write;
}


bool
DevFsNodeWrapper::HasIO() const
{
	return fCapabilities.io;
}


status_t
DevFsNodeWrapper::InitDevice()
{
	if (fIsFinalized)
		return B_DEV_NOT_READY;

	AcquireReference();

	return B_OK;
}


void
DevFsNodeWrapper::UninitDevice()
{
	ReleaseReference();
}


void
DevFsNodeWrapper::Finalize()
{
	dprintf("%p.DevFsNodeWrapper::Finalize()\n", this);
	fIsFinalized = true;
	ReleaseReference();
}


status_t
DevFsNodeWrapper::Open(const char* path, int openMode, void** _cookie)
{
	if (fIsFinalized)
		return B_DEV_NOT_READY;

	return fDevFsNode->Open(path, openMode, (DevFsNodeHandle**)_cookie);
}


status_t
DevFsNodeWrapper::_DoIO(void* cookie, off_t pos,
	void* buffer, size_t* _length, bool isWrite)
{
	IORequest request;
	status_t status = request.Init(pos, (addr_t)buffer, *_length, isWrite, 0);
	if (status != B_OK)
		return status;

	status = IO(cookie, &request);
	if (status != B_OK)
		return status;

	status = request.Wait(0, 0);
	*_length = request.TransferredBytes();
	return status;
}


status_t
DevFsNodeWrapper::Read(void* cookie, off_t pos, void* buffer, size_t* _length)
{
	if (!fCapabilities.read) {
		if (!fCapabilities.io)
			return BaseDevice::Read(cookie, pos, buffer, _length);

		return _DoIO(cookie, pos, buffer, _length, false);
	}

	if (fIsFinalized)
		return B_DEV_NOT_READY;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->Read(pos, buffer, _length);
}


status_t
DevFsNodeWrapper::Write(void* cookie, off_t pos, const void* buffer, size_t* _length)
{
	if (!fCapabilities.write) {
		if (!fCapabilities.io)
			return BaseDevice::Write(cookie, pos, buffer, _length);

		return _DoIO(cookie, pos, const_cast<void*>(buffer), _length, true);
	}

	if (fIsFinalized)
		return B_DEV_NOT_READY;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->Write(pos, buffer, _length);
}


status_t
DevFsNodeWrapper::IO(void* cookie, io_request* request)
{
	if (!fCapabilities.io)
		return BaseDevice::IO(cookie, request);

	if (fIsFinalized)
		return B_DEV_NOT_READY;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->IO(request);
}


status_t
DevFsNodeWrapper::Control(void* cookie, int32 op, void* buffer, size_t length)
{
	if (!fCapabilities.control)
		return BaseDevice::Control(cookie, op, buffer, length);

	if (fIsFinalized)
		return B_DEV_NOT_READY;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->Control(op, buffer, length);
}


status_t
DevFsNodeWrapper::Select(void* cookie, uint8 event, selectsync* sync)
{
	if (!fCapabilities.select)
		return BaseDevice::Select(cookie, event, sync);

	if (fIsFinalized)
		return B_DEV_NOT_READY;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->Select(event, sync);
}


status_t
DevFsNodeWrapper::Deselect(void* cookie, uint8 event, selectsync* sync)
{
	if (!fCapabilities.select)
		return BaseDevice::Deselect(cookie, event, sync);

	if (fIsFinalized)
		return B_DEV_NOT_READY;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->Deselect(event, sync);
}


status_t
DevFsNodeWrapper::Close(void* cookie)
{
	if (fIsFinalized)
		return B_OK;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	return handle->Close();
}


status_t
DevFsNodeWrapper::Free(void* cookie)
{
	if (fIsFinalized)
		return B_OK;

	DevFsNodeHandle* handle = (DevFsNodeHandle*)cookie;
	handle->Free();
	return B_OK;
}
