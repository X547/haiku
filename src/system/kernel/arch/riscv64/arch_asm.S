/*
 * Copyright 2019, Haiku, Inc. All Rights Reserved
 * Distributed under the terms of the MIT License.
 */


#include <asm_defs.h>
#include <arch_cpu_defs.h>


.text


/* status_t arch_cpu_user_memcpy(void *to, const void *from, size_t size, addr_t *faultHandler) */
FUNCTION(_arch_cpu_user_memcpy):
	// TODO
	ret
FUNCTION_END(_arch_cpu_user_memcpy)


/* status_t arch_cpu_user_memset(void *to, char c, size_t count, addr_t *faultHandler) */
FUNCTION(_arch_cpu_user_memset):
	// TODO
	ret
FUNCTION_END(_arch_cpu_user_memset)


/* ssize_t arch_cpu_user_strlcpy(void *to, const void *from, size_t size, addr_t *faultHandler) */
FUNCTION(_arch_cpu_user_strlcpy):
	// TODO
	ret
FUNCTION_END(_arch_cpu_user_strlcpy)
