#include "RootDevice.h"

#include <new>

#include <KernelExport.h>

#include <AutoDeleter.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


struct BusDriverDeleter : MethodDeleter<BusDriver, void, &BusDriver::Free>
{
	typedef MethodDeleter<BusDriver, void, &BusDriver::Free> Base;

	BusDriverDeleter() : Base() {}
	BusDriverDeleter(BusDriver* object) : Base(object) {}
};


status_t
RootDevice::InitDriver(DeviceNode* node)
{
	fNode = node;
	return B_OK;
}


void
RootDevice::Free()
{
	dprintf("RootDevice::Free()\n");
	dprintf("  fNode: %p\n", fNode);
	delete this;
}


const device_attr*
RootDevice::Attributes() const
{
	static const device_attr rootAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Devices Root"}},
		{B_DEVICE_BUS, B_STRING_TYPE, {.string = "root"}},
		{B_DEVICE_FLAGS, B_UINT32_TYPE,
			{.ui32 = B_FIND_MULTIPLE_CHILDREN }},
		{}
	};

	static const device_attr genericAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Generic"}},
		{B_DEVICE_BUS, B_STRING_TYPE, {.string = "generic"}},
		{B_DEVICE_FLAGS, B_UINT32_TYPE, {.ui32 = B_FIND_MULTIPLE_CHILDREN}},
		{NULL}
	};

	static const device_attr childAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Root subnode"}},
		{NULL}
	};

	return fIsRoot ? rootAttrs : childAttrs;
}


status_t
RootDevice::CreateChildNode(DeviceNode** outNode)
{
	BusDriverDeleter busDriver(new(std::nothrow) RootDevice(false));
	if (!busDriver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(fNode->RegisterNode(busDriver.Detach(), outNode));

	return B_OK;
}
