/*
 * Copyright 2003-2006, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


#include <arch/debug_console.h>
#include <boot/kernel_args.h>
#include <kernel.h>
#include <vm/vm.h>

#include <string.h>


struct HtifRegs
{
	uint32 toHostLo, toHostHi;
	uint32 fromHostLo, fromHostHi;
};

HtifRegs *volatile gHtifRegs = (HtifRegs *volatile)0x40008000;


uint64_t HtifCmd(uint32_t device, uint8_t cmd, uint32_t arg)
{
	uint64_t htifTohost = ((uint64_t)device << 56) + ((uint64_t)cmd << 48) + arg;
	gHtifRegs->toHostLo = htifTohost % ((uint64_t)1 << 32);
	gHtifRegs->toHostHi = htifTohost / ((uint64_t)1 << 32);
	return (uint64_t)gHtifRegs->fromHostLo + ((uint64_t)gHtifRegs->fromHostHi << 32);
}


void
arch_debug_remove_interrupt_handler(uint32 line)
{
}


void
arch_debug_install_interrupt_handlers(void)
{
}


int
arch_debug_blue_screen_try_getchar(void)
{
	return 0;
}


char
arch_debug_blue_screen_getchar(void)
{
	return 0;
}


int
arch_debug_serial_try_getchar(void)
{
	// TODO: Implement correctly!
	return arch_debug_serial_getchar();
}


char
arch_debug_serial_getchar(void)
{
	return 0;
}


void
arch_debug_serial_putchar(const char c)
{
	HtifCmd(1, 1, c);
}


void
arch_debug_serial_puts(const char *s)
{
	while (*s != '\0') {
		arch_debug_serial_putchar(*s);
		s++;
	}
}


void
arch_debug_serial_early_boot_message(const char *string)
{
	arch_debug_serial_puts(string); 
}


status_t
arch_debug_console_init(kernel_args *args)
{
	return B_OK;
}


status_t
arch_debug_console_init_settings(kernel_args *args)
{
	return B_OK;
}


