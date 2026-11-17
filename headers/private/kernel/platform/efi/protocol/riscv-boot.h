// Copyright 2026 X512
// Released under the terms of the MIT License

#pragma once

#include <efi/types.h>
#include <efi/boot-services.h>

#define RISCV_EFI_BOOT_PROTOCOL_GUID \
	{0xccd15fec, 0x6f73, 0x4eec, { 0x83, 0x95, 0x3e, 0x69, 0xe4, 0xb9, 0x40, 0xbf}}

extern efi_guid RiscvBootProtocol;

#define RISCV_EFI_BOOT_PROTOCOL_REVISION 0x00010000
#define RISCV_EFI_BOOT_PROTOCOL_LATEST_VERSION RISCV_EFI_BOOT_PROTOCOL_REVISION

typedef struct riscv_efi_boot_protocol {
    uint64_t Revision;

    efi_status (*GetBootHartId)(riscv_efi_boot_protocol* self, uint64_t* bootHartId);
} riscv_efi_boot_protocol;
