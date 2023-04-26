#include "StarfiveReset.h"

#include <debug.h>

#include <KernelExport.h>


#define AONCRG_RESET_ASSERT	0x38
#define AONCRG_RESET_STATUS	0x3C
#define ISPCRG_RESET_ASSERT	0x38
#define ISPCRG_RESET_STATUS	0x3C
#define VOUTCRG_RESET_ASSERT	0x48
#define VOUTCRG_RESET_STATUS	0x4C
#define STGCRG_RESET_ASSERT	0x74
#define STGCRG_RESET_STATUS	0x78
#define SYSCRG_RESET_ASSERT0	0x2F8
#define SYSCRG_RESET_ASSERT1	0x2FC
#define SYSCRG_RESET_ASSERT2	0x300
#define SYSCRG_RESET_ASSERT3	0x304
#define SYSCRG_RESET_STATUS0	0x308
#define SYSCRG_RESET_STATUS1	0x30C
#define SYSCRG_RESET_STATUS2	0x310
#define SYSCRG_RESET_STATUS3	0x314

enum JH7110_RESET_CRG_GROUP {
	SYSCRG_0 = 0,
	SYSCRG_1,
	SYSCRG_2,
	SYSCRG_3,
	STGCRG,
	AONCRG,
	ISPCRG,
	VOUTCRG,
};


StarfiveReset::MmioRange::MmioRange(phys_addr_t physAdr, size_t size):
	size(size)
{
	area.SetTo(map_physical_memory("StarfiveReset MMIO", physAdr, size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&regs));

	ASSERT_ALWAYS(area.IsSet()/*, "can't map StarFive reset MMIO registers"*/);
}

StarfiveReset::StarfiveReset():
	// TODO: read from FDT
	fSyscrg(0x13020000, 0x10000),
	fStgcrg(0x10230000, 0x10000),
	fAoncrg(0x17000000, 0x10000),
	fIspcrg(0x19810000, 0x10000),
	fVoutcrg(0x295C0000, 0x10000)
{
}

bool StarfiveReset::IsAsserted(uint32 id)
{
	AssertAndStatus assertAndStatus {};
	if (!GetAssertAndStatus(id, assertAndStatus))
		return false;

	uint32 mask = 1 << (id % 32);
	uint32 value = *assertAndStatus.status;

	return (value & mask) != 0;
}

status_t StarfiveReset::SetAsserted(uint32 id, bool doAssert)
{
	AssertAndStatus assertAndStatus {};
	if (!GetAssertAndStatus(id, assertAndStatus))
		return EINVAL;

	uint32 mask = 1 << (id % 32);
	uint32 done = 0;

	uint32 value = *assertAndStatus.assert;
	if (doAssert) {
		value |= mask;
	} else {
		value &= ~mask;
		done ^= mask;
	}

	*assertAndStatus.assert = value;

	uint32 attempts = 10000;
	do {
		value = *assertAndStatus.status;
	} while((value & mask) != done && --attempts != 0);

	return B_OK;
}

bool StarfiveReset::GetAssertAndStatus(uint32 id, AssertAndStatus& res)
{
	uint32 group = id / 32;
	switch (group) {
		case SYSCRG_0:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT0 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS0 / 4
			};
			return true;
		case SYSCRG_1:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT1 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS1 / 4
			};
			return true;
		case SYSCRG_2:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT2 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS2 / 4
			};
			return true;
		case SYSCRG_3:
			res = AssertAndStatus{
				fSyscrg.regs + SYSCRG_RESET_ASSERT3 / 4,
				fSyscrg.regs + SYSCRG_RESET_STATUS3 / 4
			};
			return true;
		case STGCRG:
			res = AssertAndStatus{
				fSyscrg.regs + STGCRG_RESET_ASSERT / 4,
				fSyscrg.regs + STGCRG_RESET_STATUS / 4
			};
			return true;
		case AONCRG:
			res = AssertAndStatus{
				fSyscrg.regs + AONCRG_RESET_ASSERT / 4,
				fSyscrg.regs + AONCRG_RESET_STATUS / 4
			};
			return true;
		case ISPCRG:
			res = AssertAndStatus{
				fSyscrg.regs + ISPCRG_RESET_ASSERT / 4,
				fSyscrg.regs + ISPCRG_RESET_STATUS / 4
			};
			return true;
		case VOUTCRG:
			res = AssertAndStatus{
				fSyscrg.regs + VOUTCRG_RESET_ASSERT / 4,
				fSyscrg.regs + VOUTCRG_RESET_STATUS / 4
			};
			return true;
	}
	return false;
}
