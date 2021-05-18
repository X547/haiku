/*
 * Copyright 2007-2010, François Revol, revol@free.fr.
 * Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2019, Adrien Destugues, pulkomandy@pulkomandy.tk.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <KernelExport.h>
#include <kernel.h>
#include <boot/kernel_args.h>
#include <vm/vm.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <arch_cpu_defs.h>
#include "RISCV64VMTranslationMap.h"
#include <Htif.h>
#include <Plic.h>
#include <Clint.h>


#define TRACE_VM_TMAP
#ifdef TRACE_VM_TMAP
#	define TRACE(x...) dprintf(x)
#else
#	define TRACE(x...) ;
#endif


HtifRegs  *volatile gHtifRegs  = (HtifRegs  *volatile)0x40008000;
PlicRegs  *volatile gPlicRegs  = (PlicRegs  *volatile)0x40100000;
ClintRegs *volatile gClintRegs = (ClintRegs *volatile)0x02000000;


phys_addr_t sPageTable = 0;
bool sPagingEnabled = false;
char sPhysicalPageMapperData[sizeof(RISCV64VMPhysicalPageMapper)];


static inline void *VirtFromPhys(uint64_t physAdr)
{
	if (!sPagingEnabled)
		return (void*)physAdr;
	return (void*)(physAdr + (KERNEL_PMAP_BASE - 0x80000000));
}

static inline uint64_t PhysFromVirt(void *virtAdr)
{
	if (!sPagingEnabled)
		return (uint64)virtAdr;
	return (uint64)virtAdr - (KERNEL_PMAP_BASE - 0x80000000);
}


static phys_addr_t AllocPhysPage(kernel_args* args)
{
	phys_addr_t page;
	page = (args->physical_allocated_range[args->num_physical_allocated_ranges - 1].start + args->physical_allocated_range[args->num_physical_allocated_ranges - 1].size) / B_PAGE_SIZE;
	args->physical_allocated_range[args->num_physical_allocated_ranges - 1].size += B_PAGE_SIZE;
	return page;
}

static Pte* LookupPte(addr_t virtAdr, bool alloc, kernel_args* args, phys_addr_t (*get_free_page)(kernel_args *))
{
	Pte *pte = (Pte*)VirtFromPhys(sPageTable);
	for (int level = 2; level > 0; level --) {
		pte += PhysAdrPte(virtAdr, level);
		if (!((1 << pteValid) & pte->flags)) {
			if (!alloc)
				return NULL;
			pte->ppn = get_free_page(args);
			if (pte->ppn == 0)
				return NULL;
			memset((Pte*)VirtFromPhys(pageSize*pte->ppn), 0, pageSize);
			pte->flags |= (1 << pteValid);
		}
		pte = (Pte*)VirtFromPhys(pageSize*pte->ppn);
	}
	pte += PhysAdrPte(virtAdr, 0);
	return pte;
}

static void Map(addr_t virtAdr, phys_addr_t physAdr, uint64 flags, kernel_args* args, phys_addr_t (*get_free_page)(kernel_args *))
{
	// dprintf("Map(0x%" B_PRIxADDR ", 0x%" B_PRIxADDR ")\n", virtAdr, physAdr);
	Pte* pte = LookupPte(virtAdr, true, args, get_free_page);
	if (pte == NULL) panic("can't allocate page table");

	pte->ppn = physAdr / B_PAGE_SIZE;
	pte->flags = (1 << pteValid) | flags;

	if (sPagingEnabled) FlushTlbPage(virtAdr);
}

static void MapRange(addr_t virtAdr, phys_addr_t physAdr, size_t size, uint64 flags, kernel_args* args, phys_addr_t (*get_free_page)(kernel_args *))
{
	dprintf("MapRange(0x%" B_PRIxADDR ", 0x%" B_PRIxADDR ", 0x%" B_PRIxADDR ")\n", virtAdr, physAdr, size);
	for (size_t i = 0; i < size; i += B_PAGE_SIZE)
		Map(virtAdr + i, physAdr + i, flags, args, get_free_page);
}

static void PreallocKernelRange(kernel_args *args)
{
	Pte *root = (Pte*)VirtFromPhys(sPageTable);
	// NOTE: adjust range if changing kernel address space range
	for (int i = 0; i < 256; i++) {
		Pte *pte = &root[i];
		pte->ppn = AllocPhysPage(args);
		if (pte->ppn == 0) panic("can't alloc early physical page");
		memset(VirtFromPhys(pageSize*pte->ppn), 0, pageSize);
		pte->flags |= (1 << pteValid);
	}
}

void EnablePaging()
{
	SatpReg satp;
	satp.ppn = sPageTable / B_PAGE_SIZE;
	satp.asid = 0;
	satp.mode = satpModeSv39;
	SetSatp(satp.val);
	FlushTlbAll();
	sPagingEnabled = true;
}

status_t
arch_vm_translation_map_init(kernel_args *args,
	VMPhysicalPageMapper** _physicalPageMapper)
{
	TRACE("vm_translation_map_init: entry\n");
	
	sPageTable = AllocPhysPage(args) * B_PAGE_SIZE;
	PreallocKernelRange(args);

	// TODO: don't hardcode RAM base
	MapRange(KERNEL_PMAP_BASE, 0x80000000, args->physical_memory_range[0].size, (1 << pteRead) | (1 << pteWrite), args, AllocPhysPage);

	for (uint32 i = 0; i < args->num_virtual_allocated_ranges; i++) {
		addr_t start = args->virtual_allocated_range[i].start;
		size_t size = args->virtual_allocated_range[i].size;
		MapRange(start, start, size, (1 << pteRead) | (1 << pteWrite) | (1 << pteExec), args, AllocPhysPage);
	}

	// TODO: read from FDT
	// CLINT
	MapRange( 0x2000000,  0x2000000,  0xC0000, (1 << pteRead) | (1 << pteWrite), args, AllocPhysPage);
	// HTIF
	MapRange(0x40008000, 0x40008000,   0x1000, (1 << pteRead) | (1 << pteWrite), args, AllocPhysPage);
	// PLIC
	MapRange(0x40100000, 0x40100000, 0x400000, (1 << pteRead) | (1 << pteWrite), args, AllocPhysPage);
	
	{
		SstatusReg status(Sstatus());
		status.sum = 1;
		SetSstatus(status.val);
	}

	EnablePaging();

	*_physicalPageMapper = new(&sPhysicalPageMapperData) RISCV64VMPhysicalPageMapper();

#ifdef TRACE_VM_TMAP
	TRACE("physical memory ranges:\n");
	for (uint32 i = 0; i < args->num_physical_memory_ranges; i++) {
		phys_addr_t start = args->physical_memory_range[i].start;
		phys_addr_t end = start + args->physical_memory_range[i].size;
		TRACE("  %" B_PRIxPHYSADDR " - %" B_PRIxPHYSADDR "\n", start,
			end);
	}

	TRACE("allocated physical ranges:\n");
	for (uint32 i = 0; i < args->num_physical_allocated_ranges; i++) {
		phys_addr_t start = args->physical_allocated_range[i].start;
		phys_addr_t end = start + args->physical_allocated_range[i].size;
		TRACE("  %" B_PRIxPHYSADDR " - %" B_PRIxPHYSADDR "\n", start,
			end);
	}

	TRACE("allocated virtual ranges:\n");
	for (uint32 i = 0; i < args->num_virtual_allocated_ranges; i++) {
		addr_t start = args->virtual_allocated_range[i].start;
		addr_t end = start + args->virtual_allocated_range[i].size;
		TRACE("  %" B_PRIxADDR " - %" B_PRIxADDR "\n", start, end);
	}
#endif

	return B_OK;
}


status_t
arch_vm_translation_map_init_post_sem(kernel_args *args)
{
	return B_OK;
}


status_t
arch_vm_translation_map_init_post_area(kernel_args *args)
{
	TRACE("vm_translation_map_init_post_area: entry\n");
	return B_OK;
}


status_t
arch_vm_translation_map_early_map(kernel_args *args, addr_t va, phys_addr_t pa,
	uint8 attributes, phys_addr_t (*get_free_page)(kernel_args *))
{
	uint64 flags = 0;
	if ((attributes & B_KERNEL_READ_AREA)    != 0) flags |= (1 << pteRead);
	if ((attributes & B_KERNEL_WRITE_AREA)   != 0) flags |= (1 << pteWrite);
	if ((attributes & B_KERNEL_EXECUTE_AREA) != 0) flags |= (1 << pteExec);
	Map(va, pa, flags, args, get_free_page);
	return B_OK;
}


status_t
arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** _map)
{
	*_map = new(std::nothrow) RISCV64VMTranslationMap(kernel, (kernel) ? sPageTable : 0);

	if (*_map == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


bool
arch_vm_translation_map_is_kernel_page_accessible(addr_t virtualAddress,
	uint32 protection)
{
	return virtualAddress != 0;
}

