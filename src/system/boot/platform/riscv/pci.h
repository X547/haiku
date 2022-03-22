#ifndef _PCI_H_
#define _PCI_H_

#include <SupportDefs.h>
#include <boot/addr_range.h>


struct PciInitInfo {
	addr_range configRegs;
	const void *intMap;
	int intMapSize;
	const void *intMapMask;
	int intMapMaskSize;
	const void *ranges;
	int rangesSize;
};


void pci_init0(PciInitInfo *info);
void pci_init();


#endif	// _PCI_H_
