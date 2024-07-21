#pragma once

#include <AutoDeleterOS.h>


enum {
	GPOUT_LOW = 0,
	GPOUT_HIGH = 1,
};

enum {
	GPOEN_ENABLE = 0,
	GPOEN_DISABLE = 1,
};

union Pinmux {
	struct {
		uint32 pin: 8;
		uint32 function: 2;
		uint32 doen: 6;
		uint32 dout: 8;
		uint32 din: 8;
	};
	uint32 val;
};


class StarfivePinCtrl {
private:
	AreaDeleter fArea;
	size_t fSize {};
	uint32 volatile *fRegs {};

public:
	StarfivePinCtrl(phys_addr_t physAdr, size_t size);

	status_t SetPinmux(uint32 pin, uint32 dout, uint32 doen);
};
