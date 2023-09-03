#include "HIDDevice.h"

#include <dm2/bus/HID.h>

#include "ProtocolHandler.h"


status_t
HIDDevice::Init(HidDevice* device, uint16 maxInputSize)
{
	fHidDevice = device;
	fMaxInputSize = maxInputSize;
	fInputBuffer.SetTo(new(std::nothrow) uint8[maxInputSize]);
	if (!fInputBuffer.IsSet())
		return B_NO_MEMORY;

	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList, fProtocolHandlerCount);
	return B_OK;
}


status_t
HIDDevice::Open(ProtocolHandler *handler, uint32 flags)
{
	atomic_add(&fOpenCount, 1);
	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler *handler)
{
	atomic_add(&fOpenCount, -1);
	return B_OK;
}


void
HIDDevice::Removed()
{
	fRemoved = true;
}


status_t
HIDDevice::MaybeScheduleTransfer(HIDReport *report)
{
	if (fRemoved)
		return ENODEV;

	status_t res = fHidDevice->RequestRead(fMaxInputSize, &fInputBuffer[0], static_cast<HidInputCallback*>(this));

	// already scheduled
	if (res == B_BUSY)
		return B_OK;

	return res;
}


status_t
HIDDevice::SendReport(HIDReport *report)
{
	// TODO
	return B_OK;
}


ProtocolHandler*
HIDDevice::ProtocolHandlerAt(uint32 index) const
{
	ProtocolHandler *handler = fProtocolHandlerList;
	while (handler != NULL) {
		if (index == 0)
			return handler;

		handler = handler->NextHandler();
		index--;
	}

	return NULL;
}


void
HIDDevice::InputAvailable(status_t status, uint8* data, uint32 actualSize)
{
	fParser.SetReport(status, data, actualSize);
}
