/*
 * Copyright 2005-2009, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_x86_INT_H
#define _KERNEL_ARCH_x86_INT_H


#define ARCH_INTERRUPT_BASE	0x20
#define NUM_IO_VECTORS		(256 - ARCH_INTERRUPT_BASE)


enum irq_source {
	IRQ_SOURCE_INVALID,
	IRQ_SOURCE_IOAPIC,
	IRQ_SOURCE_MSI,
};


static inline void
arch_int_enable_interrupts_inline(void)
{
	asm volatile("sti");
}


static inline int
arch_int_disable_interrupts_inline(void)
{
	size_t flags;

	asm volatile("pushf;\n"
		"pop %0;\n"
		"cli" : "=g" (flags));
	return (flags & 0x200) != 0;
}


static inline void
arch_int_restore_interrupts_inline(int oldState)
{
	if (oldState)
		asm volatile("sti");
}


static inline bool
arch_int_are_interrupts_enabled_inline(void)
{
	size_t flags;

	asm volatile("pushf;\n"
		"pop %0;\n" : "=g" (flags));
	return (flags & 0x200) != 0;
}


// map the functions to the inline versions
#define arch_int_enable_interrupts()	arch_int_enable_interrupts_inline()
#define arch_int_disable_interrupts()	arch_int_disable_interrupts_inline()
#define arch_int_restore_interrupts(status)	\
	arch_int_restore_interrupts_inline(status)
#define arch_int_are_interrupts_enabled()	\
	arch_int_are_interrupts_enabled_inline()


#ifdef __cplusplus


void x86_set_irq_source(int irq, irq_source source);


#endif // __cplusplus

#endif /* _KERNEL_ARCH_x86_INT_H */
