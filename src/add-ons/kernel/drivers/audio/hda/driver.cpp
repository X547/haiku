/*
 * Copyright 2007-2012, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Ithamar Adema, ithamar AT unet DOT nl
 */


#include <new>

#include <dm2/bus/PCI.h>

#include <kernel.h>
#include <lock.h>
#include <util/AutoLock.h>
#include <AutoDeleter.h>
#include <AutoDeleterOS.h>

#include "driver.h"


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define HDA_DRIVER_MODULE_NAME "drivers/audio/hda/driver/v1"


pci_module_info* gPci;


class HdaDevFsNode: public DevFsNode, public DevFsNodeHandle {
public:
	HdaDevFsNode(hda_controller* controller): fController(controller) {}
	virtual ~HdaDevFsNode() = default;

	Capabilities GetCapabilities() const final;
	status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;
	status_t Close() final;
	status_t Control(uint32 op, void *buffer, size_t length, bool isKernel) final;

private:
	hda_controller* fController;
};


class HdaDriver: public DeviceDriver {
public:
	HdaDriver(DeviceNode* node): fNode(node), fDevFsNode(&fController) {}
	virtual ~HdaDriver() = default;

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

private:
	friend class HdaDevFsNodeHandle;
	friend class HdaDevFsNode;

	DeviceNode*		fNode;
	PciDevice* 		fPciDevice {};
	HdaDevFsNode	fDevFsNode;
	hda_controller	fController {};
	char			fName[B_OS_NAME_LENGTH] {};
};


// #pragma mark - HdaDriver

status_t
HdaDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<HdaDriver> driver(new(std::nothrow) HdaDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
HdaDriver::Init()
{
	fPciDevice = fNode->QueryBusInterface<PciDevice>();

	fPciDevice->GetPciInfo(&fController.pci_info);

	static int32 lastId = 0;
	int32 id = lastId++;
	sprintf(fName, DEVFS_PATH_FORMAT, id);
	fController.devfs_path = fName;

	CHECK_RET(fNode->RegisterDevFsNode(fName, &fDevFsNode));

	return B_OK;
}


// #pragma mark - HdaDevFsNode

DevFsNode::Capabilities
HdaDevFsNode::GetCapabilities() const
{
	return {
		.control = true
	};
}


status_t
HdaDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	if (atomic_get(&fController->opened) != 0)
		return B_BUSY;

	CHECK_RET(hda_hw_init(fController));

	atomic_add(&fController->opened, 1);

	// optional user-settable buffer frames and count
	get_settings_from_file();

	*outHandle = static_cast<DevFsNodeHandle*>(this);
	return B_OK;

}


status_t
HdaDevFsNode::Close()
{
	hda_hw_stop(fController);
	atomic_add(&fController->opened, -1);
	return B_OK;
}


status_t
HdaDevFsNode::Control(uint32 op, void* arg, size_t length, bool isKernel)
{
	if (fController->active_codec != NULL)
		return multi_audio_control(fController->active_codec, op, arg, length);

	return B_BAD_VALUE;
}


static driver_module_info sHdaDriverModule = {
	.info = {
		.name = HDA_DRIVER_MODULE_NAME,
	},
	.probe = HdaDriver::Probe
};


_EXPORT module_dependency module_dependencies[] = {
	{B_PCI_MODULE_NAME, (module_info**)&gPci},
	{}
};

_EXPORT module_info* modules[] = {
	(module_info* )&sHdaDriverModule,
	NULL
};
