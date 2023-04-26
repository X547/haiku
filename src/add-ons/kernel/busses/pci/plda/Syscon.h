#pragma once

#include <AutoDeleterOS.h>


class Syscon {
private:
	AreaDeleter fArea;
	size_t fSize {};
	uint32 volatile *fRegs {};

public:
	Syscon(phys_addr_t physAdr, size_t size);

	void SetBits(uint32 index, uint32 mask, uint32 value);
};
