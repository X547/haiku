#include "Syscon.h"

#include <debug.h>

#include <KernelExport.h>


Syscon::Syscon(phys_addr_t physAdr, size_t size):
	fSize(size)
{
	fArea.SetTo(map_physical_memory("Syscon MMIO", physAdr, size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));

	ASSERT_ALWAYS(fArea.IsSet()/*, "can't map syscon MMIO registers"*/);
}

void Syscon::SetBits(uint32 index, uint32 mask, uint32 value)
{
	if (index >= fSize / 4)
		return;

	uint32 volatile *regs = fRegs + index;
	uint32 oldValue = *regs;
	*regs = (oldValue & ~mask) | (value & mask);
}
