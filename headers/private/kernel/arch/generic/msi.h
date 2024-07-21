/*
 * Copyright 2022, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _KERNEL_ARCH_GENERIC_MSI_H
#define _KERNEL_ARCH_GENERIC_MSI_H

#include <SupportDefs.h>


#ifdef __cplusplus

class MSIInterface {
public:
	static inline const char ifaceName[] = "kernel/msi";

	virtual status_t AllocateVectors(
		uint32 count, uint32& startVector, uint64& address, uint32& data) = 0;
	virtual void FreeVectors(uint32 count, uint32 startVector) = 0;

protected:
	~MSIInterface() = default;
};


extern "C" {
MSIInterface* msi_interface();
void msi_set_interface(MSIInterface* interface);
#endif

bool		msi_supported();
status_t	msi_allocate_vectors(uint32 count, uint32 *startVector,
				uint64 *address, uint32 *data);
void		msi_free_vectors(uint32 count, uint32 startVector);

#ifdef __cplusplus
}
#endif


#endif	// _KERNEL_ARCH_GENERIC_MSI_H
