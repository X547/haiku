#pragma once

#include <dm2/bus/PCI.h>

#include <util/ExternalAllocator.h>


class PCIResourceAllocator {
public:
	status_t Register(const pci_resource_range& range);
	status_t Alloc(uint32 kind, phys_addr_t& adr, uint64 size);
	status_t AllocAt(uint32 kind, phys_addr_t adr, uint64 size);
	void Free(uint32 kind, phys_addr_t adr, uint64 size);

private:
	ExternalAllocator* GetResource(uint32 kind);

private:
	ExternalAllocator fIoPortResource;
	ExternalAllocator fMmio32Resource;
	ExternalAllocator fMmio64PrefechResource;
};
