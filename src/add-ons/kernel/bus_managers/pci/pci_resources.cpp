#include "pci_resources.h"

#include <KernelExport.h>

#include <kernel.h>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


status_t
PCIResourceAllocator::Register(const pci_resource_range& range)
{
	ExternalAllocator* resource = GetResource(range.type);
	if (resource == NULL)
		return B_OK;

	if (range.pci_addr != 0)
		return resource->Register(range.pci_addr, range.size);
	else
		return resource->Register(range.pci_addr + 1, range.size - 1);
}


status_t
PCIResourceAllocator::Alloc(uint32 kind, phys_addr_t& adr, uint64 size)
{
	ExternalAllocator* resource = GetResource(kind);
	if (resource == NULL)
		return ENOENT;

	uint64 adr64;
	CHECK_RET(resource->AllocAligned(adr64, size, size));

	adr = adr64;
	return B_OK;
}


status_t
PCIResourceAllocator::AllocAt(uint32 kind, phys_addr_t adr, uint64 size)
{
	if (adr == 0)
		return B_OK;

	ExternalAllocator* resource = GetResource(kind);
	if (resource == NULL)
		return ENOENT;

	CHECK_RET(resource->AllocAt(adr, size));

	return B_OK;
}


void
PCIResourceAllocator::Free(uint32 kind, phys_addr_t adr, uint64 size)
{
	ExternalAllocator* resource = GetResource(kind);
	if (resource == NULL)
		return;

	resource->Free(adr/*, size*/);
}


ExternalAllocator*
PCIResourceAllocator::GetResource(uint32 kind)
{
	if (kind == kPciRangeMmio + kPciRangeMmio64Bit) {
		kind += kPciRangeMmioPrefetch;
	}
	switch (kind) {
		case kPciRangeIoPort:
			return &fIoPortResource;
		case kPciRangeMmio:
			return &fMmio32Resource;
		case kPciRangeMmio + kPciRangeMmio64Bit + kPciRangeMmioPrefetch:
			return &fMmio64PrefechResource;
		default:
			return NULL;
	}
}
