/*
 * Copyright 2019, Adrien Destugues, pulkomandy@pulkomandy.tk.
 * Distributed under the terms of the MIT License.
 */


#include <KernelExport.h>

#include <arch/cpu.h>
#include <arch/generic/cache_controller.h>
#include <boot/kernel_args.h>
#include <vm/VMAddressSpace.h>
#include <vm/vm_priv.h>
#include <commpage.h>
#include <elf.h>
#include <Htif.h>
#include <platform/sbi/sbi_syscalls.h>
#include <util/AutoLock.h>
#include <AutoDeleterDrivers.h>
#include "RISCV64VMTranslationMap.h"

#include <algorithm>


extern "C" void SVec();

extern uint32 gPlatform;


static CacheController* sCacheController {};
uint32 sCacheBlockSize;


status_t
install_cache_controller(CacheController* ctrl)
{
	if (sCacheController != NULL)
		return B_ERROR;

	sCacheController = ctrl;
	sCacheBlockSize = sCacheController->CacheBlockSize();
	return TRUE;
}


void
uninstall_cache_controller(CacheController* ctrl)
{
	if (sCacheController == ctrl)
		sCacheController = NULL;
}


status_t
arch_cpu_preboot_init_percpu(kernel_args *args, int curr_cpu)
{
	// dprintf("arch_cpu_preboot_init_percpu(%" B_PRId32 ")\n", curr_cpu);
	return B_OK;
}


status_t
arch_cpu_init_percpu(kernel_args *args, int curr_cpu)
{
	SetStvec((uint64)SVec);
	SstatusReg sstatus{.val = Sstatus()};
	sstatus.ie = 0;
	sstatus.fs = extStatusInitial; // enable FPU
	sstatus.xs = extStatusOff;
	SetSstatus(sstatus.val);
	SetBitsSie((1 << sTimerInt) | (1 << sSoftInt) | (1 << sExternInt));

	return B_OK;
}


status_t
arch_cpu_init(kernel_args *args)
{
	for (uint32 curCpu = 0; curCpu < args->num_cpus; curCpu++) {
		cpu_ent* cpu = &gCPU[curCpu];

		cpu->arch.hartId = args->arch_args.hartIds[curCpu];

		cpu->topology_id[CPU_TOPOLOGY_PACKAGE] = 0;
		cpu->topology_id[CPU_TOPOLOGY_CORE] = curCpu;
		cpu->topology_id[CPU_TOPOLOGY_SMT] = 0;

		for (unsigned int i = 0; i < CPU_MAX_CACHE_LEVEL; i++)
			cpu->cache_id[i] = -1;
	}

	uint64 conversionFactor
		= (1LL << 32) * 1000000LL / args->arch_args.timerFrequency;

	__riscv64_setup_system_time(conversionFactor);

	return B_OK;
}


status_t
arch_cpu_init_post_vm(kernel_args *args)
{
	// Set address space ownership to currently running threads
	for (uint32 i = 0; i < args->num_cpus; i++) {
		VMAddressSpace::Kernel()->Get();
	}

	return B_OK;
}


status_t
arch_cpu_init_post_modules(kernel_args *args)
{
	return B_OK;
}


void
arch_cpu_flush_dcache(void *address, size_t len)
{
	if (sCacheController == NULL)
		return;

	VMAddressSpacePutter addressSpace(VMAddressSpace::GetCurrent());
	auto map = (RISCV64VMTranslationMap*)addressSpace->TranslationMap();

	memory_full_barrier();
	addr_t begin = ROUNDDOWN((addr_t)address, sCacheBlockSize);
	addr_t end = ROUNDUP((addr_t)address + len, sCacheBlockSize);
	phys_addr_t physAdr = 0;
	uint32 pageFlags = 0;
	for (addr_t line = begin; line != end; line += sCacheBlockSize) {
		if (line == begin || (line % B_PAGE_SIZE) == 0) {
			map->Lock();
			map->Query(ROUNDDOWN(line, B_PAGE_SIZE), &physAdr, &pageFlags);
			map->Unlock();
		}
		if ((PAGE_PRESENT & pageFlags) == 0)
			continue;

		sCacheController->FlushCache(physAdr + (line % B_PAGE_SIZE));
		memory_full_barrier();
	}
}


void
arch_cpu_invalidate_dcache(void *address, size_t len)
{
	arch_cpu_flush_dcache(address, len);
}


void
arch_cpu_sync_icache(void *address, size_t len)
{
	FenceI();

	if (smp_get_num_cpus() > 1) {
		memory_full_barrier();
		sbi_remote_fence_i(0, -1);
	}
}


void
arch_cpu_invalidate_TLB_range(addr_t start, addr_t end)
{
	addr_t kernelStart = std::max<addr_t>(start, KERNEL_BASE);
	addr_t kernelEnd   = std::min<addr_t>(end,   KERNEL_TOP);

	addr_t userStart = std::max<addr_t>(start, USER_BASE);
	addr_t userEnd   = std::min<addr_t>(end,   USER_TOP);

	if (kernelStart <= kernelEnd) {
		int64 numPages = kernelStart / B_PAGE_SIZE - kernelEnd / B_PAGE_SIZE;
		while (numPages-- >= 0) {
			FlushTlbPage(start);
			start += B_PAGE_SIZE;
		}
	}

	if (userStart <= userEnd) {
		int64 numPages = userStart / B_PAGE_SIZE - userEnd / B_PAGE_SIZE;
		while (numPages-- >= 0) {
			FlushTlbPageAsid(start, 0);
			start += B_PAGE_SIZE;
		}
	}
}


void
arch_cpu_invalidate_TLB_list(addr_t pages[], int num_pages)
{
	for (int i = 0; i < num_pages; i++) {
		addr_t page = pages[i];
		if (IS_KERNEL_ADDRESS(page))
			FlushTlbPage(page);
		else
			FlushTlbPageAsid(page, 0);
	}
}


void
arch_cpu_global_TLB_invalidate(void)
{
	FlushTlbAll();
}


void
arch_cpu_user_TLB_invalidate(void)
{
	FlushTlbAllAsid(0);
}


status_t
arch_cpu_shutdown(bool reboot)
{
	if (gPlatform == kPlatformSbi) {
		sbi_system_reset(
			reboot ? SBI_RESET_TYPE_COLD_REBOOT : SBI_RESET_TYPE_SHUTDOWN,
			SBI_RESET_REASON_NONE);
	}

	HtifShutdown();
	return B_ERROR;
}
