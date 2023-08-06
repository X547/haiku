#include <stdio.h>

#include <new>

#include <DPC.h>
#include <KernelExport.h>

#include <dm2/bus/HID.h>

#include <AutoDeleter.h>
#include <lock.h>
#include <util/AutoLock.h>

#include "DeviceList.h"
#include "ProtocolHandler.h"
#include "HIDDevice.h"

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define HID_INPUT_DRIVER_MODULE_NAME "drivers/input/hid_input/driver/v1"


DeviceList *gDeviceList = NULL;
static mutex sDriverLock;


class HidInputDriver;


class HidInputDevFsNodeHandle: public DevFsNodeHandle {
public:
	HidInputDevFsNodeHandle(ProtocolHandler* handler): fHandler(handler) {}
	virtual ~HidInputDevFsNodeHandle();

	void Free() final {delete this;}
	status_t Close() final;
	status_t Read(off_t pos, void* buffer, size_t* _length) final;
	status_t Write(off_t pos, const void* buffer, size_t* _length) final;
	status_t Control(uint32 op, void *buffer, size_t length) final;

public:
	uint32 fCookie {};

private:
	ProtocolHandler* fHandler;
};


class HidInputDevFsNode: public DevFsNode {
public:
	HidInputDevFsNode() {}
	virtual ~HidInputDevFsNode() = default;

	Capabilities GetCapabilities() const final;
	status_t Open(const char* path, int openMode, DevFsNodeHandle **outHandle) final;
};


class HidInputDriver: public DeviceDriver, private HidDeviceCallback, private DPCCallback {
public:
	HidInputDriver(DeviceNode* node): fNode(node) {}
	virtual ~HidInputDriver();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}

private:
	status_t Init();

	void InputAvailable() final;
	void DoDPC(DPCQueue* queue) final;

private:
	DeviceNode* fNode;
	HidDevice* fHidDevice {};
	HidInputDevFsNode fDevFsNode;

	HIDDevice fHandler;

	const uint8* fReportDescriptor {};
	size_t fReportDescriptorLength {};
	uint16 fMaxInputSize {};
	uint16 fMaxOutputSize {};

	ArrayDeleter<uint8> fInputBuffer;

	int32 fDpcQueued {};
};


// #pragma mark - HidInputDevFsNodeHandle

HidInputDevFsNodeHandle::~HidInputDevFsNodeHandle()
{
	mutex_lock(&sDriverLock);

	HIDDevice *device = fHandler->Device();
	if (device->IsOpen()) {
		// another handler of this device is still open so we can't free it
	} else if (device->IsRemoved()) {
		// the parent device is removed already and none of its handlers are
		// open anymore so we can free it here
		delete device;
	}

	mutex_unlock(&sDriverLock);
}


status_t
HidInputDevFsNodeHandle::Close()
{
	return fHandler->Close(&fCookie);
}


status_t
HidInputDevFsNodeHandle::Read(off_t pos, void* buffer, size_t* _length)
{
	return fHandler->Read(&fCookie, pos, buffer, _length);
}


status_t
HidInputDevFsNodeHandle::Write(off_t pos, const void* buffer, size_t* _length)
{
	return fHandler->Write(&fCookie, pos, buffer, _length);
}


status_t
HidInputDevFsNodeHandle::Control(uint32 op, void *buffer, size_t length)
{
	return fHandler->Control(&fCookie, op, buffer, length);
}


// #pragma mark - HidInputDevFsNode

DevFsNode::Capabilities
HidInputDevFsNode::GetCapabilities() const
{
	return {
		.read = true,
		.write = true,
		.control = true
	};
}


status_t
HidInputDevFsNode::Open(const char* path, int openMode, DevFsNodeHandle **outHandle)
{
	ProtocolHandler* handler = (ProtocolHandler *)gDeviceList->FindDevice(path);
	if (handler == NULL)
		return B_ENTRY_NOT_FOUND;

	ObjectDeleter<HidInputDevFsNodeHandle> handle(new(std::nothrow) HidInputDevFsNodeHandle(handler));
	if (!handle.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(handler->Open(openMode, &handle->fCookie));

	*outHandle = handle.Detach();
	return B_OK;
}


// #pragma mark - HidInputDriver

HidInputDriver::~HidInputDriver()
{

}


status_t
HidInputDriver::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<HidInputDriver> driver(new(std::nothrow) HidInputDriver(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init());
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
HidInputDriver::Init()
{
	dprintf("HidInputDriver::Init()\n");
	fHidDevice = fNode->QueryBusInterface<HidDevice>();

	CHECK_RET(fNode->FindAttrUint16(HID_DEVICE_MAX_INPUT_SIZE, &fMaxInputSize));
	CHECK_RET(fNode->FindAttrUint16(HID_DEVICE_MAX_OUTPUT_SIZE, &fMaxOutputSize));
	CHECK_RET(fNode->FindAttr(HID_DEVICE_REPORT_DESC, B_RAW_TYPE, 0, (const void**)&fReportDescriptor, &fReportDescriptorLength));
	dprintf("  fMaxInputSize: %" B_PRIu16 "\n", fMaxInputSize);
	dprintf("  fMaxOutputSize: %" B_PRIu16 "\n", fMaxOutputSize);
	dprintf("  fReportDescriptorLength: %" B_PRIuSIZE "\n", fReportDescriptorLength);

	for (size_t i = 0; i < fReportDescriptorLength; i++) {
		dprintf(" %02x", fReportDescriptor[i]);
		if (i % 16 == 15)
			dprintf("\n");
	}
	if (fReportDescriptorLength % 16 != 0)
		dprintf("\n");

	fInputBuffer.SetTo(new(std::nothrow) uint8[fMaxInputSize]);
	if (!fInputBuffer.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fHandler.Parser().ParseReportDescriptor(fReportDescriptor, fReportDescriptorLength));
	CHECK_RET(fHandler.Init());

	for (uint32 i = 0;; i++) {
		ProtocolHandler *handler = fHandler.ProtocolHandlerAt(i);
		if (handler == NULL)
			break;

		// As devices can be un- and replugged at will, we cannot
		// simply rely on a device count. If there is just one
		// keyboard, this does not mean that it uses the 0 name.
		// There might have been two keyboards and the one using 0
		// might have been unplugged. So we just generate names
		// until we find one that is not currently in use.
		int32 index = 0;
		char pathBuffer[128];
		const char *basePath = handler->BasePath();
		while (true) {
			sprintf(pathBuffer, "%s%" B_PRId32, basePath, index++);
			if (gDeviceList->FindDevice(pathBuffer) == NULL) {
				// this name is still free, use it
				handler->SetPublishPath(strdup(pathBuffer));
				break;
			}
		}

		gDeviceList->AddDevice(handler->PublishPath(), handler);

		fNode->RegisterDevFsNode(pathBuffer, &fDevFsNode);
	}

	fHidDevice->SetCallback(static_cast<HidDeviceCallback*>(this));

	return B_OK;
}


void
HidInputDriver::InputAvailable()
{
	//dprintf("HidInputDriver::InputAvailable()\n");

	if (atomic_get_and_set(&fDpcQueued, 1) == 0)
		DPCQueue::DefaultQueue(B_URGENT_DISPLAY_PRIORITY)->Add(this);
}


void
HidInputDriver::DoDPC(DPCQueue* queue)
{
	//dprintf("HidInputDriver::DoDPC()\n");
	atomic_set(&fDpcQueued, 0);

	fHidDevice->Read(fMaxInputSize, &fInputBuffer[0]);

	uint16 actualLength = fInputBuffer[0] | (fInputBuffer[1] << 8);

	if (actualLength == 0) {
		// handle reset
		return;
	}

	if (actualLength <= 2)
		actualLength = 0;
	else
		actualLength -= 2;

	fHandler.Parser().SetReport(B_OK, &fInputBuffer[2], actualLength);
}


// #pragma mark -

static status_t
std_ops(int32 op, ...)
{
	switch (op) {
		case B_MODULE_INIT:
			gDeviceList = new(std::nothrow) DeviceList();
			if (gDeviceList == NULL)
				return B_NO_MEMORY;

			mutex_init(&sDriverLock, "hid input driver lock");
			return B_OK;

		case B_MODULE_UNINIT:
			delete gDeviceList;
			gDeviceList = NULL;
			mutex_destroy(&sDriverLock);
			return B_OK;

		default:
			break;
	}

	return B_ERROR;
}


static driver_module_info sHidInputDriverModule = {
	.info = {
		.name = HID_INPUT_DRIVER_MODULE_NAME,
		.std_ops = std_ops
	},
	.probe = HidInputDriver::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sHidInputDriverModule,
	NULL
};
