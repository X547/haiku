#pragma once

#include <AutoDeleterOS.h>


class StarfiveReset {
private:
	struct MmioRange {
		AreaDeleter area;
		size_t size {};
		uint32 volatile *regs {};

		MmioRange(phys_addr_t physAdr, size_t size);
	};

	struct AssertAndStatus {
		uint32 volatile *assert;
		uint32 volatile *status;
	};

	MmioRange fSyscrg;
	MmioRange fStgcrg;
	MmioRange fAoncrg;
	MmioRange fIspcrg;
	MmioRange fVoutcrg;

	bool GetAssertAndStatus(uint32 id, AssertAndStatus& res);

public:
	StarfiveReset();

	bool IsAsserted(uint32 id);
	status_t SetAsserted(uint32 id, bool doAssert);
};
