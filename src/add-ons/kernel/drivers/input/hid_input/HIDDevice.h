#pragma once


#include "HIDParser.h"


class ProtocolHandler;


class HIDDevice {
public:
								HIDDevice(): fParser(this) {}
								~HIDDevice() = default;

			status_t			Init();

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
			int32				fOpenCount {};
			bool				fRemoved {};

			HIDParser			fParser;

			uint32				fProtocolHandlerCount {};
			ProtocolHandler *	fProtocolHandlerList {};
};
