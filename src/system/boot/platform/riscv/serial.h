/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */

#ifndef SERIAL_H
#define SERIAL_H


#include <SupportDefs.h>
#include <boot/uart.h>


class DebugUART;

extern DebugUART* gUART;


void serial_puts(const char *string, size_t size);
void serial_puts(const char *string);

void serial_disable();
void serial_enable();
void serial_init(uart_info *info);


#endif	/* SERIAL_H */
