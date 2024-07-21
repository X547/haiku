/*
 * Copyright 2009-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2008-2009, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef ABSTRACT_MODULE_DEVICE_H
#define ABSTRACT_MODULE_DEVICE_H


#include "BaseDevice.h"


class device_node;

struct device_module_info {
	status_t (*init_device)(void *driverCookie, void **_deviceCookie);
	void (*uninit_device)(void *deviceCookie);
	void (*device_removed)(void *deviceCookie);

	status_t (*open)(void *deviceCookie, const char *path, int openMode,
					void **_cookie);
	status_t (*close)(void *cookie);
	status_t (*free)(void *cookie);
	status_t (*read)(void *cookie, off_t pos, void *buffer, size_t *_length);
	status_t (*write)(void *cookie, off_t pos, const void *buffer,
					size_t *_length);
	status_t (*io)(void *cookie, io_request *request);
	status_t (*control)(void *cookie, uint32 op, void *buffer, size_t length);
	status_t (*select)(void *cookie, uint8 event, selectsync *sync);
	status_t (*deselect)(void *cookie, uint8 event, selectsync *sync);
};


namespace BPrivate {


class AbstractModuleDevice : public BaseDevice {
public:
							AbstractModuleDevice();
	virtual					~AbstractModuleDevice();

			device_module_info* Module() const { return fDeviceModule; }

			void*			Data() const { return fDeviceData; }
			device_node*	Node() const { return fNode; }

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

protected:
	device_node*			fNode;
	int32					fInitialized;
	device_module_info*		fDeviceModule;
	void*					fDeviceData;
};


} // namespace BPrivate


using BPrivate::AbstractModuleDevice;


#endif	// ABSTRACT_MODULE_DEVICE_H
