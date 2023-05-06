#include "StarfivePinCtrl.h"

#include <debug.h>

#include <KernelExport.h>


StarfivePinCtrl::StarfivePinCtrl(phys_addr_t physAdr, size_t size):
	fSize(size)
{
	fArea.SetTo(map_physical_memory("PinCtrl MMIO", physAdr, size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));

	ASSERT_ALWAYS(fArea.IsSet()/*, "can't map pinctrl MMIO registers"*/);
}


status_t StarfivePinCtrl::SetPinmux(uint32 pin, uint32 dout, uint32 doen)
{
	uint32 offset = pin / 4;
	uint32 shift  = 8 * (pin % 4);
	uint32 doutMask = 0b1111111 << shift;
	uint32 doenMask = 0b111111 << shift;
	uint32 volatile* regDout = fRegs + 0x040 / 4 + offset;
	uint32 volatile* regDoen = fRegs + offset;

	dout <<= shift;
	doen <<= shift;

	uint32 regDoutVal = *regDout;
	dprintf("pinctrl: readl(%16" B_PRIx64 ") -> %#" B_PRIx32 "\n", (addr_t)regDout, regDoutVal);
	dout |= regDoutVal & ~doutMask;
	// dout |= *regDout & ~doutMask;
	*regDout = dout;
	dprintf("pinctrl: writel(%#" B_PRIx32 ", %16" B_PRIx64 ") -> \n", dout, (addr_t)regDout);

	uint32 regDoenVal = *regDoen;
	dprintf("pinctrl: readl(%16" B_PRIx64 ") -> %#" B_PRIx32 "\n", (addr_t)regDoen, regDoenVal);
	doen |= regDoenVal & ~doenMask;
	// doen |= *regDoen & ~doenMask;
	*regDoen = doen;
	dprintf("pinctrl: writel(%#" B_PRIx32 ", %16" B_PRIx64 ") -> \n", doen, (addr_t)regDoen);

	return B_OK;
}

/*
pcie@2B000000
	perst-default
	phandle = <0x1f>;
	pinmux = <0xff01001a>;

	perst-active
	phandle = <0x20>;
	pinmux = <0xff00001a>;

	wake-default
	phandle = <0x21>;
	pinmux = <0xff010020>;

	clkreq-default
	phandle = <0x22>;
	pinmux = <0xff01001b>;

pcie@2C000000
	perst-default
	phandle = <0x1b>;
	pinmux = <0xff01001c>;

	perst-active
	phandle = <0x1c>;
	pinmux = <0xff00001c>;

	wake-default
	phandle = <0x1d>;
	pinmux = <0xff010015>;

	clkreq-default
	phandle = <0x1e>;
	pinmux = <0xff01001d>;
*/