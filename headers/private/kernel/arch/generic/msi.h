/*
 * Copyright 2022, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_GENERIC_MSI_H
#define _KERNEL_ARCH_GENERIC_MSI_H

#include <SupportDefs.h>


#ifdef __cplusplus
extern "C" {
#endif

bool		msi_supported();
status_t	msi_allocate_vectors(uint8 count, uint8 *startVector,
				uint64 *address, uint16 *data);
void		msi_free_vectors(uint8 count, uint8 startVector);

#ifdef __cplusplus
}
#endif


#endif	// _KERNEL_ARCH_GENERIC_MSI_H
