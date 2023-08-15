/*
 * Copyright 2019-2022 Haiku, Inc. All rights reserved.
 * Released under the terms of the MIT License.
 */

#include "string.h"

#include <boot/platform.h>
#include <boot/stage2.h>
#include <arch_acpi.h>
#include <arch_smp.h>

#include "serial.h"
#include "acpi.h"

#include <arch/arm/arch_uart_pl011.h>
#include <arch/generic/debug_uart_8250.h>


template <typename T> DebugUART*
get_uart(addr_t base, int64 clock) {
	static char buffer[sizeof(T)];
	return new(buffer) T(base, clock);
}


void
arch_handle_acpi()
{
	platform_cpu_info* info;
	arch_smp_register_cpu(&info);
	if (info == NULL)
		panic("arch_smp_register_cpu failed");

	// TODO
	info->id = 0;
	info->phandle = 0;
	info->plicContext = 0;

	{
		uart_info &uart = gKernelArgs.arch_args.uart;
		strcpy(uart.kind, UART_KIND_8250);
		uart.regs.start = 0x10000000;
		uart.regs.size = 0x100;
		uart.irq = 0;
		uart.clock = 0;

		gUART = get_uart<DebugUART8250>(uart.regs.start, uart.clock);
	}

	acpi_spcr *spcr = (acpi_spcr*)acpi_find_table(ACPI_SPCR_SIGNATURE);
	if (spcr != NULL) {
		uart_info &uart = gKernelArgs.arch_args.uart;

		if (spcr->interface_type == ACPI_SPCR_INTERFACE_TYPE_16550) {
			strcpy(uart.kind, UART_KIND_8250);
		}

		uart.regs.start = spcr->base_address.address;
		uart.regs.size = B_PAGE_SIZE;
		uart.irq = spcr->gisv;
		uart.clock = spcr->clock;

		dprintf("discovered uart from acpi: base=%lx, irq=%u, clock=%lu\n",
			uart.regs.start, uart.irq, uart.clock);
	}

#if 0
	acpi_madt *madt = (acpi_madt*)acpi_find_table(ACPI_MADT_SIGNATURE);
	if (madt != NULL) {
		uint64 gicc_base = 0;
		uint64 gicd_base = 0;
		uint8 version = 0;

		acpi_apic *desc = (acpi_apic*)(madt + 1);
		while (desc != (acpi_apic*)((char*)madt + madt->header.length)) {
			if (desc->type == ACPI_MADT_GIC_INTERFACE) {
				acpi_gic_interface *acpi_gicc = (acpi_gic_interface*)desc;
				if (acpi_gicc->cpu_interface_num == 0)
					gicc_base = acpi_gicc->base_address;
			} else if (desc->type == ACPI_MADT_GIC_DISTRIBUTOR) {
				acpi_gic_distributor *acpi_gicd = (acpi_gic_distributor*)desc;
				gicd_base = acpi_gicd->base_address;
				version = acpi_gicd->gic_version;
			}
			desc = (acpi_apic*)((char*)desc + desc->length);
		}

		if (version == 2 && gicc_base != 0 && gicd_base != 0) {
			intc_info &intc = gKernelArgs.arch_args.interrupt_controller;
			strcpy(intc.kind, INTC_KIND_GICV2);
			intc.regs1.start = gicd_base;
			intc.regs2.start = gicc_base;

			dprintf("discovered gic from acpi: version=%d, gicd=%lx, gicc=%lx\n",
				version, gicd_base, gicc_base);
		}
	}
#endif
}
