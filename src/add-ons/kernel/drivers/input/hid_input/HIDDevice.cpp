#include "HIDDevice.h"

#include "ProtocolHandler.h"


status_t
HIDDevice::Init()
{
	ProtocolHandler::AddHandlers(*this, fProtocolHandlerList, fProtocolHandlerCount);
	return B_OK;
}


status_t
HIDDevice::Open(ProtocolHandler *handler, uint32 flags)
{
	return B_OK;
}


status_t
HIDDevice::Close(ProtocolHandler *handler)
{
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

	return B_OK;
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
