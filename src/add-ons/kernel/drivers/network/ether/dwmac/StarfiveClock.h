#pragma once

#include <AutoDeleterOS.h>


union StarfiveClockRegs {
	struct {
		uint32 div: 24;
		uint32 mux: 6;
		uint32 invert: 1;
		uint32 enable: 1;
	};
	uint32 val;
};


class StarfiveClock {
private:
	struct MmioRange {
		AreaDeleter area;
		size_t size {};
		StarfiveClockRegs volatile *regs {};

		MmioRange(phys_addr_t physAdr, size_t size);
	};

	MmioRange fSys;
	MmioRange fStg;
	MmioRange fAon;

	bool GetRegs(uint32 id, StarfiveClockRegs volatile*& res);

public:
	StarfiveClock();

	bool IsEnabled(uint32 id);
	status_t SetEnabled(uint32 id, bool doEnable);
};
