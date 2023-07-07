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

	bool GetRegs(int32 id, StarfiveClockRegs volatile*& res);

public:
	StarfiveClock();

	bool IsEnabled(int32 id);
	status_t SetEnabled(int32 id, bool doEnable);

	int64 GetRate(int32 id);
	status_t SetRate(int32 id, int64 rate /* Hz */);

	int32 GetParent(int32 id);
	status_t SetParent(int32 id, int32 parentId);
};
