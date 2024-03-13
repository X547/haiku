/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "DWPCIController.h"

#include <int.h>


status_t
MsiInterruptCtrlDW::Init(PciDbiRegs volatile* dbiRegs, int32 msiIrq)
{
	dprintf("MsiInterruptCtrlDW::Init()\n");
	dprintf("  msiIrq: %" B_PRId32 "\n", msiIrq);

	fDbiRegs = dbiRegs;

	// TODO: Detect actually supported maximum interrupt count (at least 32, but can be bigger).
	fMaxMsiCount = 32;
	CHECK_RET(fAllocatedMsiIrqs.Resize(fMaxMsiCount));

	physical_entry pe;
	status_t result = get_memory_map(&fMsiData, sizeof(fMsiData), &pe, 1);
	if (result != B_OK) {
		dprintf("  unable to get MSI Memory map!\n");
		return result;
	}

	fMsiPhysAddr = pe.address;
	dprintf("  fMsiPhysAddr: %#" B_PRIxADDR "\n", fMsiPhysAddr);
	fDbiRegs->msiAddrLo = (uint32)fMsiPhysAddr;
	fDbiRegs->msiAddrHi = (uint32)(fMsiPhysAddr >> 32);

	fDbiRegs->msiIntr[0].enable = 0xffffffff;
	fDbiRegs->msiIntr[0].mask = 0xffffffff;

	result = install_io_interrupt_handler(msiIrq, InterruptReceived, this, 0);
	if (result != B_OK) {
		dprintf("  unable to attach MSI irq handler!\n");
		return result;
	}
	result = allocate_io_interrupt_vectors_ex(32, &fMsiStartIrq, INTERRUPT_TYPE_IRQ, static_cast<InterruptSource*>(this));

	if (result != B_OK) {
		dprintf("  unable to attach MSI irq handler!\n");
		return result;
	}

	msi_set_interface(static_cast<MSIInterface*>(this));

	dprintf("  fMsiStartIrq: %ld\n", fMsiStartIrq);

	return B_OK;
}


status_t
MsiInterruptCtrlDW::AllocateVectors(uint8 count, uint8& startVector, uint64& address, uint16& data)
{
	if (count < 1)
		return B_ERROR;

	ssize_t index = fAllocatedMsiIrqs.GetLowestContiguousClear(count);
	if (index < 0)
		return B_ERROR;

	fAllocatedMsiIrqs.SetRange(index, count);
	for (ssize_t i = index; i < index + count; i++)
		fDbiRegs->msiIntr[i / 32].mask &= ~(1 << i);

	startVector = fMsiStartIrq + index;
	address = fMsiPhysAddr;
	data = index;
	return B_OK;
}


void
MsiInterruptCtrlDW::FreeVectors(uint8 count, uint8 startVector)
{
	int32 irq = (int32)startVector - fMsiStartIrq;

	for (ssize_t i = irq; i < irq + count; i++)
		fDbiRegs->msiIntr[i / 32].mask |= (1 << (uint32)i);

	fAllocatedMsiIrqs.ClearRange(irq, count);
}


int32
MsiInterruptCtrlDW::InterruptReceived(void* arg)
{
	return static_cast<MsiInterruptCtrlDW*>(arg)->InterruptReceivedInt();
}


int32
MsiInterruptCtrlDW::InterruptReceivedInt()
{
//	dprintf("MsiInterruptCtrlDW::InterruptReceivedInt()\n");
	uint32 status = fDbiRegs->msiIntr[0].status;
	for (int i = 0; i < 32; i++) {
		if (((1 << i) & status) != 0) {
//			dprintf("MSI IRQ: %d (%ld)\n", i, fStartMsiIrq + i);
			int_io_interrupt_handler(fMsiStartIrq + i, false);
			fDbiRegs->msiIntr[0].status = (1 << i);
		}
	}
	return B_HANDLED_INTERRUPT;
}


void
MsiInterruptCtrlDW::EnableIoInterrupt(int vector)
{
	dprintf("MsiInterruptCtrlDW::EnableIoInterrupt(%d)\n", vector);
	uint32 irq = vector - fMsiStartIrq;
	fDbiRegs->msiIntr[irq / 32].enable |= 1 << (irq % 32);
}


void
MsiInterruptCtrlDW::DisableIoInterrupt(int vector)
{
	dprintf("MsiInterruptCtrlDW::DisableIoInterrupt(%d)\n", vector);
	uint32 irq = vector - fMsiStartIrq;
	fDbiRegs->msiIntr[irq / 32].enable &= ~(1 << (irq % 32));
}


void
MsiInterruptCtrlDW::ConfigureIoInterrupt(int vector, uint32 config)
{
}


int32
MsiInterruptCtrlDW::AssignToCpu(int32 vector, int32 cpu)
{
	return 0;
}
