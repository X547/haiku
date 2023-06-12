#include "StarfiveClock.h"
#include "starfive-jh7110-clkgen.h"

#include <debug.h>

#include <KernelExport.h>


#define SYS_OFFSET(id) ((id))
#define STG_OFFSET(id) (((id) - JH7110_CLK_SYS_REG_END))
#define AON_OFFSET(id) (((id) - JH7110_CLK_STG_REG_END))
#define VOUT_OFFSET(id) (((id) - JH7110_CLK_VOUT_START))


StarfiveClock::MmioRange::MmioRange(phys_addr_t physAdr, size_t size):
	size(size)
{
	area.SetTo(map_physical_memory("StarfiveClock MMIO", physAdr, size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&regs));

	ASSERT_ALWAYS(area.IsSet()/*, "can't map StarFive clock MMIO registers"*/);
}

StarfiveClock::StarfiveClock():
	// TODO: read from FDT
	fSys(0x13020000, 0x10000),
	fStg(0x10230000, 0x10000),
	fAon(0x17000000, 0x10000)
{
}

bool StarfiveClock::IsEnabled(uint32 id)
{
	StarfiveClockRegs volatile* regs {};
	if (!GetRegs(id, regs))
		return false;

	return regs->enable;
}

status_t StarfiveClock::SetEnabled(uint32 id, bool doEnable)
{
	StarfiveClockRegs volatile* regs {};
	if (!GetRegs(id, regs))
		return ENOENT;

	StarfiveClockRegs regsVal {.val = regs->val};
	dprintf("clk-gate: readl(%16" B_PRIx64 ") -> %#" B_PRIx32 "\n", (addr_t)regs, regsVal.val);
	regsVal.enable = doEnable;
	regs->val = regsVal.val;
	dprintf("clk-gate: writel(%#" B_PRIx32 ", %16" B_PRIx64 ") -> \n", regsVal.val, (addr_t)regs);

	// regs->enable = doEnable;
	return B_OK;
}

uint64 StarfiveClock::GetRate(uint32 id)
{
	// TODO: implement
	return 0;
}

status_t StarfiveClock::SetRate(uint32 id, uint64 rate)
{
	// TODO: implement
	return ENOSYS;
}

bool StarfiveClock::GetRegs(uint32 id, StarfiveClockRegs volatile*& res)
{
	switch (id) {
		case JH7110_NOC_BUS_CLK_STG_AXI:
		case JH7110_GMAC0_GTXCLK:
		case JH7110_GMAC0_PTP:
		case JH7110_GMAC0_GTXC:
		case JH7110_GMAC1_GTXCLK:
		case JH7110_GMAC5_CLK_TX:
		case JH7110_GMAC5_CLK_PTP:
		case JH7110_GMAC5_CLK_AHB:
		case JH7110_GMAC5_CLK_AXI:
		case JH7110_GMAC1_GTXC:
		case JH7110_GMAC1_RMII_RTX:
			res = fSys.regs + SYS_OFFSET(id);
			return true;
		case JH7110_PCIE0_CLK_TL:
		case JH7110_PCIE0_CLK_AXI_MST0:
		case JH7110_PCIE0_CLK_APB:
		case JH7110_PCIE1_CLK_TL:
		case JH7110_PCIE1_CLK_AXI_MST0:
		case JH7110_PCIE1_CLK_APB:
			res = fStg.regs + STG_OFFSET(id);
			return true;
		case JH7110_U0_GMAC5_CLK_TX:
		case JH7110_U0_GMAC5_CLK_AHB:
		case JH7110_U0_GMAC5_CLK_AXI:
		case JH7110_GMAC0_RMII_RTX:
			res = fSys.regs + AON_OFFSET(id);
			return true;
	}
	return false;
}
