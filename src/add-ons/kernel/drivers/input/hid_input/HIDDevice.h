#pragma once


#include <dm2/bus/HID.h>

// !!! conflicts with HIDReport.h
#undef HID_REPORT_TYPE_INPUT
#undef HID_REPORT_TYPE_OUTPUT
#undef HID_REPORT_TYPE_FEATURE

#include <AutoDeleter.h>

#include "HIDParser.h"


class ProtocolHandler;


class HIDDevice final: private HidInputCallback {
public:
								HIDDevice(): fParser(this) {}
								~HIDDevice() = default;

			status_t			Init(HidDevice* device, uint16 maxInputSize);

			bool				IsOpen() const { return fOpenCount > 0; }
			status_t			Open(ProtocolHandler *handler, uint32 flags);
			status_t			Close(ProtocolHandler *handler);
			int32				OpenCount() const { return fOpenCount; }

			void				Removed();
			bool				IsRemoved() const { return fRemoved; }

			status_t			MaybeScheduleTransfer(HIDReport *report);

			status_t			SendReport(HIDReport *report);

			HIDParser &			Parser() { return fParser; }
			ProtocolHandler *	ProtocolHandlerAt(uint32 index) const;

private:
			// HidInputCallback
			void				InputAvailable(status_t status, uint8* data, uint32 actualSize) final;

private:
			int32				fOpenCount {};
			bool				fRemoved {};

			HIDParser			fParser;

			uint32				fProtocolHandlerCount {};
			ProtocolHandler*	fProtocolHandlerList {};

			HidDevice*			fHidDevice {};
			uint16				fMaxInputSize {};
			ArrayDeleter<uint8> fInputBuffer;
};
