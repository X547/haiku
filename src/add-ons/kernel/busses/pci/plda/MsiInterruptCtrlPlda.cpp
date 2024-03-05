/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */


#include "PciControllerPlda.h"


status_t
MsiInterruptCtrlPlda::Init(PciPldaRegs volatile* regs, int32 irq)
{
	dprintf("MsiInterruptCtrlPlda::Init()\n");
	dprintf("  irq: %" B_PRId32 "\n", irq);

	fRegs = regs;

	fRegs->istatusLocal.val = 0xffffffff;
	fRegs->imaskLocal.val = /*kPciPldaIntLegacy.val |*/ kPciPldaIntErrors.val | PciPldaInt{.msi = true}.val;

	fMsiPhysAddr = fRegs->imsiAddr;
	dprintf("  fMsiPhysAddr: %#" B_PRIxADDR "\n", fMsiPhysAddr);

	status_t result = install_io_interrupt_handler(irq, InterruptReceived, this, 0);
	if (result != B_OK) {
		dprintf("  unable to attach MSI irq handler!\n");
		return result;
	}
	result = allocate_io_interrupt_vectors_ex(32, &fMsiStartIrq, INTERRUPT_TYPE_IRQ,
		static_cast<InterruptSource*>(this));

	if (result != B_OK) {
		dprintf("  unable to attach MSI irq handler!\n");
		return result;
	}

	msi_set_interface(static_cast<MSIInterface*>(this));

	dprintf("  fMsiStartIrq: %" B_PRId32 "\n", fMsiStartIrq);

	return B_OK;
}


status_t
MsiInterruptCtrlPlda::AllocateVectors(uint32 count, uint32& startVector, uint64& address, uint32& data)
{
	dprintf("MsiInterruptCtrlPlda::AllocateVectors(%" B_PRIu32 ")\n", count);
	if (count != 1)
		return B_ERROR;

	for (int i = 0; i < 32; i++) {
		if (((1 << i) & fAllocatedMsiIrqs[0]) == 0) {
			fAllocatedMsiIrqs[0] |= (1 << i);

			startVector = fMsiStartIrq + i;
			address = fMsiPhysAddr;
			data = i;
			dprintf("  startVector: %" B_PRIu32 "\n", startVector);
			dprintf("  address: %#" B_PRIx64 "\n", address);
			dprintf("  data: %#" B_PRIx32 "\n", data);
			return B_OK;
		}
	}
	return B_ERROR;
}


void
MsiInterruptCtrlPlda::FreeVectors(uint32 count, uint32 startVector)
{
	dprintf("MsiInterruptCtrlPlda::FreeVectors(%" B_PRIu32 ", %" B_PRIu32 ")\n", count, startVector);
	int32 irq = (int32)startVector - fMsiStartIrq;
	while (count > 0) {
		if (irq >= 0 && irq < 32 && ((1 << irq) & fAllocatedMsiIrqs[0]) != 0) {
			fAllocatedMsiIrqs[0] &= ~(1 << (uint32)irq);
		}
		irq++;
		count--;
	}
}


int32
MsiInterruptCtrlPlda::InterruptReceived(void* arg)
{
	return static_cast<MsiInterruptCtrlPlda*>(arg)->InterruptReceivedInt();
}


int32
MsiInterruptCtrlPlda::InterruptReceivedInt()
{
	//dprintf("MsiInterruptCtrlPlda::InterruptReceivedInt()\n");

	PciPldaInt status {.val = fRegs->istatusLocal.val & kPciPldaIntAll.val};
	if (status.msi) {
		uint32 statusMsi = fRegs->istatusMsi;
		for (int i = 0; i < 32; i++) {
			if (((1 << i) & statusMsi) != 0) {
				fRegs->istatusMsi = (1 << i);
				//dprintf("MSI IRQ: %d (%ld)\n", i, fMsiStartIrq + i);
				int_io_interrupt_handler(fMsiStartIrq + i, false);
			}
		}
		fRegs->istatusLocal.val = PciPldaInt{.msi = true}.val;
		status.msi = false;
	}

	if (status.val != 0) {
		dprintf("  [!] unhandled PCI interrupts: %#" B_PRIx32 "\n", status.val);
		fRegs->istatusLocal.val = status.val;
	}

	return B_HANDLED_INTERRUPT;
}


void
MsiInterruptCtrlPlda::EnableIoInterrupt(int vector)
{
	dprintf("MsiInterruptCtrlPlda::EnableIoInterrupt(%d)\n", vector);
}


void
MsiInterruptCtrlPlda::DisableIoInterrupt(int vector)
{
	dprintf("MsiInterruptCtrlPlda::DisableIoInterrupt(%d)\n", vector);
}


void
MsiInterruptCtrlPlda::EndOfInterrupt(int vector)
{
}


void
MsiInterruptCtrlPlda::ConfigureIoInterrupt(int vector, uint32 config)
{
}


int32
MsiInterruptCtrlPlda::AssignToCpu(int32 vector, int32 cpu)
{
	return 0;
}
