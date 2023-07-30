/*
 * Copyright 2018, Your Name <your@email.address>
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include "FdtBus.h"

#include <AutoDeleter.h>
#include <debug.h>

extern "C" {
#include <libfdt_env.h>
#include <fdt.h>
#include <libfdt.h>
};


#define FDT_BUS_DRIVER_MODULE_NAME "bus_managers/fdt/driver/v1"


#define GIC_INTERRUPT_CELL_TYPE     0
#define GIC_INTERRUPT_CELL_ID       1
#define GIC_INTERRUPT_CELL_FLAGS    2
#define GIC_INTERRUPT_TYPE_SPI      0
#define GIC_INTERRUPT_TYPE_PPI      1
#define GIC_INTERRUPT_BASE_SPI      32
#define GIC_INTERRUPT_BASE_PPI      16


extern void* gFDT;


static uint32
fdt_get_address_cells(const void* fdt, int node)
{
	uint32 res = 2;

	int parent = fdt_parent_offset(fdt, node);
	if (parent < 0)
		return res;

	uint32 *prop = (uint32*)fdt_getprop(fdt, parent, "#address-cells", NULL);
	if (prop == NULL)
		return res;

	res = fdt32_to_cpu(*prop);
	return res;
}


static uint32
fdt_get_size_cells(const void* fdt, int node)
{
	uint32 res = 1;

	int parent = fdt_parent_offset(fdt, node);
	if (parent < 0)
		return res;

	uint32 *prop = (uint32*)fdt_getprop(fdt, parent, "#size-cells", NULL);
	if (prop == NULL)
		return res;

	res = fdt32_to_cpu(*prop);
	return res;
}


static uint32
fdt_get_interrupt_parent(int node)
{
	while (node >= 0) {
		uint32* prop;
		int propLen;
		prop = (uint32*)fdt_getprop(gFDT, node, "interrupt-parent", &propLen);
		if (prop != NULL && propLen == 4) {
			return fdt32_to_cpu(*prop);
		}

		node = fdt_parent_offset(gFDT, node);
	}

	return 0;
}


static uint32
fdt_get_interrupt_cells(uint32 interrupt_parent_phandle)
{
	if (interrupt_parent_phandle > 0) {
		int node = fdt_node_offset_by_phandle(gFDT, interrupt_parent_phandle);
		if (node >= 0) {
			uint32* prop;
			int propLen;
			prop  = (uint32*)fdt_getprop(gFDT, node, "#interrupt-cells", &propLen);
			if (prop != NULL && propLen == 4) {
				return fdt32_to_cpu(*prop);
			}
		}
	}
	return 1;
}


// #pragma mark - FdtBusImpl

DeviceNode*
FdtBusImpl::NodeByPhandle(int phandle)
{
	DeviceNode** devNode;
	if (!fPhandles.Get(phandle, devNode))
		return NULL;

	(*devNode)->AcquireReference();
	return *devNode;
}


status_t
FdtBusImpl::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<FdtBusImpl> driver(new(std::nothrow) FdtBusImpl(node));
	if (!driver.IsSet())
		return B_NO_MEMORY;

	*outDriver = driver.Detach();
	return B_OK;
}


void
FdtBusImpl::Free()
{
	dprintf("FdtBusImpl::Free()\n");
	dprintf("  fNode: %p\n", fNode);
	delete this;
}


void*
FdtBusImpl::QueryInterface(const char* name)
{
	if (strcmp(name, FdtBus::ifaceName) == 0)
		return static_cast<FdtBus*>(this);

	return NULL;
}


status_t
FdtBusImpl::RegisterChildDevices()
{
	int node = -1, depth = -1;
	node = fdt_next_node(gFDT, node, &depth);
	Traverse(node, depth, fNode);

	return B_OK;
}


void
FdtBusImpl::Traverse(int &node, int &depth, DeviceNode* parentDev)
{
	int curDepth = depth;
#if 0
	for (int i = 0; i < depth; i++) dprintf("  ");
	dprintf("node('%s')\n", fdt_get_name(gFDT, node, NULL));
#endif
	DeviceNode* curDev {};
	RegisterNode(node, parentDev, curDev);

	node = fdt_next_node(gFDT, node, &depth);
	while (node >= 0 && depth == curDepth + 1) {
		Traverse(node, depth, curDev);
	}
}


status_t
FdtBusImpl::RegisterNode(int node, DeviceNode* parentDev, DeviceNode*& curDev)
{
	TRACE("%s('%s', %p)\n", __func__, fdt_get_name(gFDT, node, NULL), parentDev);

	const void* prop; int propLen;

	ObjectDeleter<FdtDeviceImpl> fdtDev(new(std::nothrow) FdtDeviceImpl(fNode, node));
	if (!fdtDev.IsSet()) {
		return B_NO_MEMORY;
	}

	CHECK_RET(parentDev->RegisterNode(static_cast<BusDriver*>(fdtDev.Detach()), &curDev));

	prop = fdt_getprop(gFDT, node, "phandle", &propLen);
	if (prop != NULL)
		fPhandles.Put(fdt32_to_cpu(*(uint32_t*)prop), curDev);

	return B_OK;
}


// #pragma mark - FdtDeviceImpl

status_t
FdtDeviceImpl::InitDriver(DeviceNode* devNode)
{
	fNode = devNode;

	const void* prop; int propLen;
	int nameLen = 0;
	const char *name = fdt_get_name(gFDT, fFdtNode, &nameLen);

	if (name == NULL) {
		dprintf("%s ERROR: fdt_get_name: %s\n", __func__, fdt_strerror(nameLen));
		return B_ERROR;
	}

	fAttrs.Add({ B_DEVICE_BUS, B_STRING_TYPE, {.string = "fdt"}});
	fAttrs.Add({ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
		{ .string = (strcmp(name, "") != 0) ? name : "Root" }});
	fAttrs.Add({ B_FDT_DEVICE_NODE, B_UINT32_TYPE, {.ui32 = (uint32)fFdtNode}});
	fAttrs.Add({ B_FDT_DEVICE_NAME, B_STRING_TYPE, {.string = name}});

	prop = fdt_getprop(gFDT, fFdtNode, "device_type", &propLen);
	if (prop != NULL)
		fAttrs.Add({ B_FDT_DEVICE_TYPE, B_STRING_TYPE, { .string = (const char*)prop }});

	prop = fdt_getprop(gFDT, fFdtNode, "compatible", &propLen);

	if (prop != NULL) {
		const char* propStr = (const char*)prop;
		const char* propEnd = propStr + propLen;
		while (propEnd - propStr > 0) {
			int curLen = strlen(propStr);
			fAttrs.Add({ B_FDT_DEVICE_COMPATIBLE, B_STRING_TYPE, { .string = propStr }});
			propStr += curLen + 1;
		}
	}

	fAttrs.Add({});

	return B_OK;
}


void
FdtDeviceImpl::Free()
{
	dprintf("FdtDeviceImpl::Free()\n");
	dprintf("  fNode: %p\n", fNode);
	delete this;
}


const device_attr*
FdtDeviceImpl::Attributes() const
{
	return &fAttrs[0];
}


void*
FdtDeviceImpl::QueryInterface(const char* name)
{
	if (strcmp(name, FdtDevice::ifaceName) == 0)
		return static_cast<FdtDevice*>(this);

	return NULL;
}


DeviceNode*
FdtDeviceImpl::GetBus()
{
	fBusNode->AcquireReference();
	return fBusNode;
}


const char*
FdtDeviceImpl::GetName()
{
	return fdt_get_name(gFDT, fFdtNode, NULL);
}


const void*
FdtDeviceImpl::GetProp(const char* name, int* len)
{
	return fdt_getprop(gFDT, fFdtNode, name, len);
}


bool
FdtDeviceImpl::GetReg(uint32 ord, uint64* regs, uint64* len)
{
	int propLen;
	const void* prop = fdt_getprop(gFDT, fFdtNode, "reg", &propLen);
	if (prop == NULL)
		return false;

	uint32 addressCells = fdt_get_address_cells(gFDT, fFdtNode);
	uint32 sizeCells = fdt_get_size_cells(gFDT, fFdtNode);
	size_t entrySize = 4 * (addressCells + sizeCells);

	if ((ord + 1) * entrySize > (uint32)propLen)
		return false;

	const void* addressPtr = (const uint8*)prop + ord * entrySize;
	const void* sizePtr = (const uint32*)addressPtr + addressCells;

	switch (addressCells) {
		case 1:
			*regs = fdt32_to_cpu(*(const uint32*)addressPtr);
			break;
		case 2:
			*regs = fdt64_to_cpu(*(const uint64*)addressPtr);
			break;
		default:
			return false;
	}
	switch (sizeCells) {
		case 1:
			*len = fdt32_to_cpu(*(const uint32*)sizePtr);
			break;
		case 2:
			*len = fdt64_to_cpu(*(const uint64*)sizePtr);
			break;
		default:
			return false;
	}

	return true;
}


bool
FdtDeviceImpl::GetInterrupt(uint32 index, DeviceNode** outInterruptController, uint64* interrupt)
{
	uint32 interruptParent = 0;
	uint32 interruptNumber = 0;

	int propLen;
	const uint32 *prop = (uint32*)fdt_getprop(gFDT, fFdtNode, "interrupts-extended", &propLen);
	if (prop == NULL) {
		interruptParent = fdt_get_interrupt_parent(fFdtNode);
		uint32 interruptCells = fdt_get_interrupt_cells(interruptParent);

		prop = (uint32*)fdt_getprop(gFDT, (int)fFdtNode, "interrupts", &propLen);
		if (prop == NULL)
			return false;

		if ((index + 1) * interruptCells * sizeof(uint32) > (uint32)propLen)
			return false;

		uint32 offset = interruptCells * index;

		if ((interruptCells == 1) || (interruptCells == 2)) {
			 interruptNumber = fdt32_to_cpu(*(prop + offset));
		} else if (interruptCells == 3) {
			uint32 interruptType = fdt32_to_cpu(prop[offset + GIC_INTERRUPT_CELL_TYPE]);
			interruptNumber = fdt32_to_cpu(prop[offset + GIC_INTERRUPT_CELL_ID]);

			if (interruptType == GIC_INTERRUPT_TYPE_SPI)
				interruptNumber += GIC_INTERRUPT_BASE_SPI;
			else if (interruptType == GIC_INTERRUPT_TYPE_PPI)
				interruptNumber += GIC_INTERRUPT_BASE_PPI;
		} else {
			panic("unsupported interruptCells");
		}
	} else {
		if ((index + 1) * 8 > (uint32)propLen)
			return false;

		interruptParent = fdt32_to_cpu(*(prop + 2 * index));
		interruptNumber = fdt32_to_cpu(*(prop + 2 * index + 1));
	}

	if (outInterruptController != NULL) {
		FdtBus* bus = fBusNode->QueryDriverInterface<FdtBus>();
		DeviceNode* intCtrlFdtNode = bus->NodeByPhandle(interruptParent);
		if (intCtrlFdtNode != NULL)
			intCtrlFdtNode->AcquireReference();

		*outInterruptController = intCtrlFdtNode;
	}

	if (interrupt != NULL)
		*interrupt = interruptNumber;

	return true;
}


FdtInterruptMap*
FdtDeviceImpl::GetInterruptMap()
{
	// TODO: implement
	return NULL;
}


// #pragma mark - FdtInterruptMapImpl

void
FdtInterruptMapImpl::Print()
{
	// TODO: implement
}


uint32
FdtInterruptMapImpl::Lookup(uint32 childAddr, uint32 childIrq)
{
	// TODO: implement
	return 0;
}


static driver_module_info sFdtBusDriver = {
	.info = {
		.name = FDT_BUS_DRIVER_MODULE_NAME,
	},
	.probe = FdtBusImpl::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sFdtBusDriver,
	NULL
};
