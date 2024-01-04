/*
 * Copyright 2004-2008, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2013-2014, Fredrik Holmqvist, fredrik.holmqvist@gmail.com.
 * Copyright 2016, Jessica Hamilton, jessica.l.hamilton@gmail.com.
 * Distributed under the terms of the MIT License.
 */


#include "serial.h"

#include <arch/generic/debug_uart.h>
#include <arch/generic/debug_uart_8250.h>
#include <arch/riscv64/arch_uart_sifive.h>

#include <new>
#include <string.h>


static bool sSerialEnabled = true;

DebugUART* gUART = NULL;


template <typename T> static DebugUART*
GetUartInstance(uart_info *info) {
	static char buffer[sizeof(T)];
	return new(buffer) T(info->regs.start, info->clock, info->reg_io_width, info->reg_shift);
}


static void
serial_putc(char ch)
{
	if (!sSerialEnabled)
		return;

	if (gUART != NULL) {
		gUART->PutChar(ch);
		return;
	}
}


void
serial_puts(const char* string, size_t size)
{
	if (!sSerialEnabled)
		return;

	while (size-- != 0) {
		char ch = string[0];

		if (ch == '\n') {
			serial_putc('\r');
			serial_putc('\n');
		} else if (ch != '\r')
			serial_putc(ch);

		string++;
	}
}


void
serial_puts(const char* string)
{
	serial_puts(string, strlen(string));
}


void
serial_disable(void)
{
	sSerialEnabled = false;
}


void
serial_enable(void)
{
	sSerialEnabled = true;
}


void
serial_init(uart_info *info)
{
	if (strcmp(info->kind, UART_KIND_8250) == 0) {
		gUART = GetUartInstance<DebugUART8250>(info);
	} else if (strcmp(info->kind, UART_KIND_SIFIVE) == 0) {
		gUART = GetUartInstance<ArchUARTSifive>(info);
	}
}
