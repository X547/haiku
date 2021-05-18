#include "fdt.h"
#include <SupportDefs.h>
#include <ByteOrder.h>
#include <KernelExport.h>
#include <boot/stage2.h>


static bool
fdt_valid(void* fdt, uint32* size)
{
	if (fdt == NULL)
		return false;
	uint32* words = (uint32*)fdt;
	if (B_BENDIAN_TO_HOST_INT32(words[0]) != 0xd00dfeed)
		return false;
	*size = B_BENDIAN_TO_HOST_INT32(words[1]);
	if (size == 0)
		return false;

	return true;
}


void
fdt_init(void* fdt)
{
	dprintf("FDT: %p\n", fdt);

	uint32 fdtSize = 0;
	if (!fdt_valid(fdt, &fdtSize)) {
		panic("Invalid FDT\n");
	}

	dprintf("FDT valid, size: %" B_PRIu32 "\n", fdtSize);

	gKernelArgs.arch_args.fdt = kernel_args_malloc(fdtSize);
	if (gKernelArgs.arch_args.fdt != NULL) {
		memcpy(gKernelArgs.arch_args.fdt, fdt, fdtSize);
	} else
		panic("unable to malloc for fdt!\n");
}
