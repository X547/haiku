#include "RootDevice.h"

#include <new>

#include <KernelExport.h>

#include "Utils.h"


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


#if 0
const device_attr*
RootDevice::Attributes() const
{
	static const device_attr genericAttrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Generic"}},
		{B_DEVICE_BUS, B_STRING_TYPE, {.string = "generic"}},
		{B_DEVICE_FLAGS, B_UINT32_TYPE, {.ui32 = B_FIND_MULTIPLE_CHILDREN}},
		{NULL}
	};


	return fIsRoot ? rootAttrs : childAttrs;
}
#endif


status_t
RootDevice::CreateChildNode(DeviceNode** outNode)
{
	BusDriverDeleter busDriver(new(std::nothrow) RootDevice(false));
	if (!busDriver.IsSet())
		return B_NO_MEMORY;

	static const device_attr attrs[] = {
		{B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Root subnode"}},
		{NULL}
	};
	CHECK_RET(fNode->RegisterNode(NULL, busDriver.Detach(), attrs, outNode));

	return B_OK;
}
