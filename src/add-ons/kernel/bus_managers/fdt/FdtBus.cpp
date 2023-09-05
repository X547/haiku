/*
 * Copyright 2014, Ithamar R. Adema <ithamar@upgrade-android.com>
 * All rights reserved. Distributed under the terms of the MIT License.
 *
 * Copyright 2015-2023, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "FdtBus.h"

#include <ByteOrder.h>

#include <dm2/device/InterruptController.h>
#include <dm2/device/Clock.h>
#include <dm2/device/Reset.h>

#include <AutoDeleterDM2.h>
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
fdt_get_interrupt_parent(const void* fdt, int node)
{
	while (node >= 0) {
		uint32* prop;
		int propLen;
		prop = (uint32*)fdt_getprop(fdt, node, "interrupt-parent", &propLen);
		if (prop != NULL && propLen == 4) {
			return fdt32_to_cpu(*prop);
		}

		node = fdt_parent_offset(fdt, node);
	}

	return 0;
}


static uint32
fdt_get_interrupt_cells(const void* fdt, uint32 interrupt_parent_phandle)
{
	if (interrupt_parent_phandle > 0) {
		int node = fdt_node_offset_by_phandle(fdt, interrupt_parent_phandle);
		if (node >= 0) {
			uint32* prop;
			int propLen;
			prop  = (uint32*)fdt_getprop(fdt, node, "#interrupt-cells", &propLen);
			if (prop != NULL && propLen == 4) {
				return fdt32_to_cpu(*prop);
			}
		}
	}
	return 1;
}


static int32
fdt_find_string(const char* prop, int size, const char* name)
{
	int32 index = 0;
	int nameLen = strlen(name);
	const char* propEnd = prop + size;
	while (propEnd - prop > 0) {
		int curLen = strlen(prop);
		if (curLen == nameLen && memcmp(prop, name, curLen + 1) == 0)
			return index;
		prop += curLen + 1;
		index++;
	}
	return B_NAME_NOT_FOUND;
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

	CHECK_RET(driver->Init());
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
FdtBusImpl::Init()
{
	if (gFDT == NULL)
		return ENODEV;

	size_t size = fdt_totalsize(gFDT);
	fFDT.SetTo(malloc(size));
	if (!fFDT.IsSet())
		return B_NO_MEMORY;

	memcpy(fFDT.Get(), gFDT, size);

	int node = -1, depth = -1;
	node = fdt_next_node(fFDT.Get(), node, &depth);
	CHECK_RET(Traverse(node, depth, fNode));

	return B_OK;
}


status_t
FdtBusImpl::Traverse(int &node, int &depth, DeviceNode* parentDev)
{
	int curDepth = depth;
#if 0
	for (int i = 0; i < depth; i++) dprintf("  ");
	dprintf("node('%s')\n", fdt_get_name(fFDT.Get(), node, NULL));
#endif
	DeviceNode* curDev {};
	CHECK_RET(RegisterNode(node, parentDev, curDev));
	DeviceNodePutter curDevDeleter(curDev);

	node = fdt_next_node(fFDT.Get(), node, &depth);
	while (node >= 0 && depth == curDepth + 1) {
		CHECK_RET(Traverse(node, depth, curDev));
	}
	return B_OK;
}


status_t
FdtBusImpl::RegisterNode(int node, DeviceNode* parentDev, DeviceNode*& curDev)
{
	TRACE("%s('%s', %p)\n", __func__, fdt_get_name(fFDT.Get(), node, NULL), parentDev);

	const void* prop; int propLen;

	ObjectDeleter<FdtDeviceImpl> fdtDev(new(std::nothrow) FdtDeviceImpl(this, node));
	if (!fdtDev.IsSet()) {
		return B_NO_MEMORY;
	}

	Vector<device_attr> attrs;
	CHECK_RET(fdtDev->BuildAttrs(attrs));
	CHECK_RET(parentDev->RegisterNode(fNode, static_cast<BusDriver*>(fdtDev.Detach()), &attrs[0], &curDev));

	prop = fdt_getprop(fFDT.Get(), node, "phandle", &propLen);
	if (prop != NULL)
		fPhandles.Put(fdt32_to_cpu(*(uint32_t*)prop), curDev);

	return B_OK;
}


// #pragma mark - FdtDeviceImpl

status_t
FdtDeviceImpl::BuildAttrs(Vector<device_attr>& attrs)
{
	const void* prop; int propLen;
	int nameLen = 0;
	const char *name = fdt_get_name(fBus->GetFDT(), fFdtNode, &nameLen);

	if (name == NULL) {
		dprintf("%s ERROR: fdt_get_name: %s\n", __func__, fdt_strerror(nameLen));
		return B_ERROR;
	}

	CHECK_RET(attrs.Add({ B_DEVICE_BUS, B_STRING_TYPE, {.string = "fdt"}}));
	CHECK_RET(attrs.Add({ B_DEVICE_PRETTY_NAME, B_STRING_TYPE,
		{ .string = (strcmp(name, "") != 0) ? name : "Root" }}));
	CHECK_RET(attrs.Add({ B_FDT_DEVICE_NODE, B_UINT32_TYPE, {.ui32 = (uint32)fFdtNode}}));
	CHECK_RET(attrs.Add({ B_FDT_DEVICE_NAME, B_STRING_TYPE, {.string = name}}));

	prop = GetProp("device_type", &propLen);
	if (prop != NULL) {
		CHECK_RET(attrs.Add({ B_FDT_DEVICE_TYPE, B_STRING_TYPE, { .string = (const char*)prop }}));
	}

	prop = GetProp("compatible", &propLen);

	if (prop != NULL) {
		const char* propStr = (const char*)prop;
		const char* propEnd = propStr + propLen;
		while (propEnd - propStr > 0) {
			int curLen = strlen(propStr);
			CHECK_RET(attrs.Add({ B_FDT_DEVICE_COMPATIBLE, B_STRING_TYPE, { .string = propStr }}));
			propStr += curLen + 1;
		}
	}

	CHECK_RET(attrs.Add({}));

	return B_OK;
}


status_t
FdtDeviceImpl::InitDriver(DeviceNode* devNode)
{
	fNode = devNode;
	return B_OK;
}


void
FdtDeviceImpl::Free()
{
	dprintf("FdtDeviceImpl::Free()\n");
	dprintf("  fNode: %p\n", fNode);
	delete this;
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
	fBus->GetNode()->AcquireReference();
	return fBus->GetNode();
}


const char*
FdtDeviceImpl::GetName()
{
	return fdt_get_name(fBus->GetFDT(), fFdtNode, NULL);
}


const void*
FdtDeviceImpl::GetProp(const char* name, int* len)
{
	return fdt_getprop(fBus->GetFDT(), fFdtNode, name, len);
}


bool
FdtDeviceImpl::GetReg(uint32 ord, uint64* regs, uint64* len)
{
	int propLen;
	const void* prop = GetProp("reg", &propLen);
	if (prop == NULL)
		return false;

	uint32 addressCells = fdt_get_address_cells(fBus->GetFDT(), fFdtNode);
	uint32 sizeCells = fdt_get_size_cells(fBus->GetFDT(), fFdtNode);
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


status_t
FdtDeviceImpl::GetRegByName(const char* name, uint64* regs, uint64* len)
{
	int propLen;
	const void* prop = GetProp("reg-names", &propLen);
	if (prop == NULL)
		return B_NAME_NOT_FOUND;

	int32 index = fdt_find_string((const char*)prop, propLen, name);
	CHECK_RET(index);

	if (!GetReg(index, regs, len))
		return B_BAD_INDEX;

	return B_OK;
}


bool
FdtDeviceImpl::GetInterrupt(uint32 index, DeviceNode** outInterruptController, uint64* interrupt)
{
	uint32 interruptParent = 0;
	uint32 interruptNumber = 0;

	int propLen;
	const uint32 *prop = (uint32*)GetProp("interrupts-extended", &propLen);
	if (prop == NULL) {
		interruptParent = fdt_get_interrupt_parent(fBus->GetFDT(), fFdtNode);
		uint32 interruptCells = fdt_get_interrupt_cells(fBus->GetFDT(), interruptParent);

		prop = (uint32*)GetProp("interrupts", &propLen);
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
		DeviceNode* intCtrlFdtNode = fBus->NodeByPhandle(interruptParent);
		if (intCtrlFdtNode != NULL)
			intCtrlFdtNode->AcquireReference();

		*outInterruptController = intCtrlFdtNode;
	}

	if (interrupt != NULL)
		*interrupt = interruptNumber;

	return true;
}


status_t
FdtDeviceImpl::GetInterruptByName(const char* name, DeviceNode** interruptController, uint64* interrupt)
{
	int propLen;
	const void* prop = GetProp("interrupt-names", &propLen);
	if (prop == NULL)
		return B_NAME_NOT_FOUND;

	int32 index = fdt_find_string((const char*)prop, propLen, name);
	CHECK_RET(index);

	if (!GetInterrupt(index, interruptController, interrupt))
		return B_BAD_INDEX;

	return B_OK;
}


FdtInterruptMap*
FdtDeviceImpl::GetInterruptMap()
{
	if (fInterruptMap.IsSet())
		return fInterruptMap.Get();

	ObjectDeleter<FdtInterruptMapImpl> interruptMap(new(std::nothrow) FdtInterruptMapImpl());
	if (!interruptMap.IsSet())
		return NULL;

	int intMapMaskLen;
	const void* intMapMask = GetProp("interrupt-map-mask", &intMapMaskLen);

	if (intMapMask == NULL || intMapMaskLen != 4 * 4) {
		dprintf("  interrupt-map-mask property not found or invalid\n");
		return NULL;
	}

	interruptMap->fChildAddrMask = B_BENDIAN_TO_HOST_INT32(*((uint32*)intMapMask + 0));
	interruptMap->fChildIrqMask = B_BENDIAN_TO_HOST_INT32(*((uint32*)intMapMask + 3));

	int intMapLen;
	const void* intMapAddr = GetProp("interrupt-map", &intMapLen);
	if (intMapAddr == NULL) {
		dprintf("  interrupt-map property not found\n");
		return NULL;
	}

	int addressCells = 3;
	int interruptCells = 1;
	int phandleCells = 1;

	const void *property;

	property = GetProp("#address-cells", NULL);
	if (property != NULL)
		addressCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);

	property = GetProp("#interrupt-cells", NULL);
	if (property != NULL)
		interruptCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);

	uint32_t *it = (uint32_t*)intMapAddr;
	while ((uint8_t*)it - (uint8_t*)intMapAddr < intMapLen) {
		FdtInterruptMapImpl::MapEntry irqEntry;

		irqEntry.childAddr = B_BENDIAN_TO_HOST_INT32(*it);
		it += addressCells;

		irqEntry.childIrq = B_BENDIAN_TO_HOST_INT32(*it);
		it += interruptCells;

		irqEntry.parentIrqCtrl = B_BENDIAN_TO_HOST_INT32(*it);
		it += phandleCells;

		int parentAddressCells = 0;
		int parentInterruptCells = 1;

		int interruptParent = fdt_node_offset_by_phandle(fBus->GetFDT(), irqEntry.parentIrqCtrl);
		if (interruptParent >= 0) {
			property = fdt_getprop(fBus->GetFDT(), interruptParent, "#address-cells", NULL);
			if (property != NULL)
				parentAddressCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);

			property = fdt_getprop(fBus->GetFDT(), interruptParent, "#interrupt-cells", NULL);
			if (property != NULL)
				parentInterruptCells = B_BENDIAN_TO_HOST_INT32(*(uint32*)property);
		}

		it += parentAddressCells;

		if ((parentInterruptCells == 1) || (parentInterruptCells == 2)) {
			irqEntry.parentIrq = B_BENDIAN_TO_HOST_INT32(*it);
		} else if (parentInterruptCells == 3) {
			uint32 interruptType = fdt32_to_cpu(it[GIC_INTERRUPT_CELL_TYPE]);
			uint32 interruptNumber = fdt32_to_cpu(it[GIC_INTERRUPT_CELL_ID]);

			if (interruptType == GIC_INTERRUPT_TYPE_SPI)
				irqEntry.parentIrq = interruptNumber + GIC_INTERRUPT_BASE_SPI;
			else if (interruptType == GIC_INTERRUPT_TYPE_PPI)
				irqEntry.parentIrq = interruptNumber + GIC_INTERRUPT_BASE_PPI;
			else
				irqEntry.parentIrq = interruptNumber;
		}
		it += parentInterruptCells;

		interruptMap->fInterruptMap.PushBack(irqEntry);
	}

	fInterruptMap.SetTo(interruptMap.Detach());
	return fInterruptMap.Get();
}


status_t
FdtDeviceImpl::GetClock(uint32 ord, ClockDevice** outClock)
{
	int clocksPropLen;
	const uint8* clocksProp = (const uint8*)GetProp("clocks", &clocksPropLen);
	if (clocksProp == NULL)
		return B_BAD_INDEX;

	for (;;) {
		if (clocksPropLen < 4)
			return B_BAD_INDEX;

		uint32 phandle = B_BENDIAN_TO_HOST_INT32(*(const uint32*)clocksProp);
		clocksProp    += 4;
		clocksPropLen -= 4;

		DeviceNode* clockCtrlNode = fBus->NodeByPhandle(phandle);
		if (clockCtrlNode == NULL)
			return B_ERROR;

		FdtDevice* clockCtrlDev = clockCtrlNode->QueryBusInterface<FdtDevice>();
		int propLen;
		const void* prop = clockCtrlDev->GetProp("#clock-cells", &propLen);
		uint32 clockCells = 0;
		if (prop != NULL) {
			if (propLen != 4)
				return B_ERROR;

			clockCells = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
		}

		if ((uint32)clocksPropLen < 4*clockCells)
			return B_BAD_INDEX;

		if (ord == 0) {
			ClockController* clockCtrl = clockCtrlNode->QueryDriverInterface<ClockController>();
			if (clockCtrl == NULL)
				return B_ERROR;

			ClockDevice* clock = clockCtrl->GetDevice(clocksProp, 4*clockCells);
			if (clock == NULL)
				return ENODEV;

			*outClock = clock;
			return B_OK;
		}

		clocksProp    += 4*clockCells;
		clocksPropLen -= 4*clockCells;

		ord--;
	}
}


status_t
FdtDeviceImpl::GetClockByName(const char* name, ClockDevice** clock)
{
	int propLen;
	const void* prop = GetProp("clock-names", &propLen);
	if (prop == NULL)
		return B_NAME_NOT_FOUND;

	int32 index = fdt_find_string((const char*)prop, propLen, name);
	CHECK_RET(index);

	return GetClock(index, clock);
}


status_t
FdtDeviceImpl::GetReset(uint32 ord, ResetDevice** outReset)
{
	int resetsPropLen;
	const uint8* resetsProp = (const uint8*)GetProp("resets", &resetsPropLen);
	if (resetsProp == NULL)
		return B_BAD_INDEX;

	for (;;) {
		if (resetsPropLen < 4)
			return B_BAD_INDEX;

		uint32 phandle = B_BENDIAN_TO_HOST_INT32(*(const uint32*)resetsProp);
		resetsProp    += 4;
		resetsPropLen -= 4;

		DeviceNode* resetCtrlNode = fBus->NodeByPhandle(phandle);
		if (resetCtrlNode == NULL)
			return B_ERROR;

		FdtDevice* resetCtrlDev = resetCtrlNode->QueryBusInterface<FdtDevice>();
		int propLen;
		const void* prop = resetCtrlDev->GetProp("#reset-cells", &propLen);
		uint32 resetCells = 0;
		if (prop != NULL) {
			if (propLen != 4)
				return B_ERROR;

			resetCells = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
		}

		if ((uint32)resetsPropLen < 4*resetCells)
			return B_BAD_INDEX;

		if (ord == 0) {
			ResetController* resetCtrl = resetCtrlNode->QueryDriverInterface<ResetController>();
			if (resetCtrl == NULL)
				return B_ERROR;

			ResetDevice* reset = resetCtrl->GetDevice(resetsProp, 4*resetCells);
			if (reset == NULL)
				return ENODEV;

			*outReset = reset;
			return B_OK;
		}

		resetsProp    += 4*resetCells;
		resetsPropLen -= 4*resetCells;

		ord--;
	}
}


status_t
FdtDeviceImpl::GetResetByName(const char* name, ResetDevice** reset)
{
	int propLen;
	const void* prop = GetProp("reset-names", &propLen);
	if (prop == NULL)
		return B_NAME_NOT_FOUND;

	int32 index = fdt_find_string((const char*)prop, propLen, name);
	CHECK_RET(index);

	return GetReset(index, reset);
}


// #pragma mark - FdtInterruptMapImpl

void
FdtInterruptMapImpl::Print()
{
	dprintf("interrupt_map_mask: 0x%08" PRIx32 ", 0x%08" PRIx32 "\n",
		fChildAddrMask, fChildIrqMask);
	dprintf("interrupt_map:\n");

	for (Vector<MapEntry>::Iterator it = fInterruptMap.Begin(); it != fInterruptMap.End(); it++) {

		dprintf("childAddr=0x%08" PRIx32 ", childIrq=%" PRIu32 ", parentIrqCtrl=%" PRIu32 ", parentIrq=%" PRIu32 "\n",
			it->childAddr, it->childIrq, it->parentIrqCtrl, it->parentIrq);
	}
}


uint32
FdtInterruptMapImpl::Lookup(uint32 childAddr, uint32 childIrq)
{
	childAddr &= fChildAddrMask;
	childIrq &= fChildIrqMask;

	for (Vector<MapEntry>::Iterator it = fInterruptMap.Begin(); it != fInterruptMap.End(); it++) {
		if ((it->childAddr == childAddr) && (it->childIrq == childIrq))
			return it->parentIrq;
	}

	return 0xffffffff;
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
