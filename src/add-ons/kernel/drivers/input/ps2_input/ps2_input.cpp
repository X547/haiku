/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include "ps2_input.h"

#include <KernelExport.h>
#include <bus/FDT.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDrivers.h>
#include <util/AutoLock.h>


enum {
	ps2CmdReset                = 0xff,
	ps2CmdResend               = 0xfe,
	ps2CmdSetDefaults          = 0xf6,
	ps2CmdDisableDataReporting = 0xf5,
	ps2CmdEnableDataReporting  = 0xf4,
	ps2CmdSetSampleRate        = 0xf3,
	ps2CmdGetDevId             = 0xf2,
};

enum {
	ps2DevIdMouseGeneric = 0x0000,
	ps2DevIdMouseWheel   = 0x0003,
	ps2DevIdKeyboard     = 0x83AB,
};


struct AlteraPs2Regs {
	union Data {
		struct {
			uint32 data:     8;
			uint32 unknown1: 7;
			uint32 isAvail:  1;
			uint32 avail:   16;
		};
		uint32 val;
	} data;
	union Control {
		struct {
			uint32 irqEnabled: 1;
			uint32 unknown1:   7;
			uint32 irqPending: 1;
			uint32 unknown2:   1;
			uint32 error:      1;
			uint32 unknown3:  21;
		};
		uint32 val;
	} control;
};


class AlteraPs2 {
private:
	struct mutex fLock = MUTEX_INITIALIZER("Altera PS/2");
	AreaDeleter fRegsArea;
	volatile AlteraPs2Regs* fRegs {};
	long fIrqVector = -1;

	ps2_interrupt_handler fInterruptHandler {};
	void* fInterruptCookie {};

public:
	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	status_t InitDriver(device_node* node);
	void UninitDriver();
	status_t RegisterChildDevices();

	static int32 HandleInterrupt(void* arg);

	status_t Read(uint8& val);
	status_t Write(uint8 val);
	void SetInterruptHandler(ps2_interrupt_handler handler, void* handlerCookie);
};


float
AlteraPs2::SupportsDevice(device_node* parent)
{
	const char* bus;
	status_t status = gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	const char* compatible;
	status = gDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "altr,ps2-1.0") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
AlteraPs2::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "Altera PS/2 Controller"} },
		{ B_DEVICE_BUS, B_STRING_TYPE, {string: "ps2"} },
		{ B_DEVICE_FIXED_CHILD, B_STRING_TYPE, {string: PS2_MOUSE_MODULE_NAME} },
		{}
	};

	return gDeviceManager->register_node(parent, PS2_MODULE_NAME, attrs, NULL, NULL);
}


status_t
AlteraPs2::InitDriver(device_node* node)
{
	dprintf("AlteraPs2::InitDriver\n");
	DeviceNodePutter<&gDeviceManager> parent(gDeviceManager->get_parent_node(node));

	const char* bus;
	CHECK_RET(gDeviceManager->get_attr_string(parent.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	fdt_device_module_info *parentModule;
	fdt_device* parentDev;
	CHECK_RET(gDeviceManager->get_driver(parent.Get(), (driver_module_info**)&parentModule, (void**)&parentDev));

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!parentModule->get_reg(parentDev, 0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("Altera PS/2 MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	if (!fRegsArea.IsSet())
		return fRegsArea.Get();

	uint64 irq;
	if (!parentModule->get_interrupt(parentDev, 0, NULL, &irq))
		return B_ERROR;
	fIrqVector = irq; // TODO: take interrupt controller into account

	install_io_interrupt_handler(fIrqVector, HandleInterrupt, this, B_NO_LOCK_VECTOR);

	fRegs->control.irqEnabled = true;

	fRegs->data.val = AlteraPs2Regs::Data{.data = ps2CmdEnableDataReporting}.val;

	dprintf(" -> OK\n");
	return B_OK;
}


void
AlteraPs2::UninitDriver()
{
	fRegs->control.irqEnabled = false;
	remove_io_interrupt_handler(fIrqVector, HandleInterrupt, this);
	delete this;
}


status_t
AlteraPs2::RegisterChildDevices()
{
	return B_OK;
}


int32
AlteraPs2::HandleInterrupt(void* arg)
{
	//dprintf("AlteraPs2::HandleInterrupt\n");
	AlteraPs2* ctrl = (AlteraPs2*)arg;

	if (ctrl->fInterruptHandler != NULL)
		return ctrl->fInterruptHandler(ctrl->fInterruptCookie);

	AlteraPs2Regs::Data data {.val = ctrl->fRegs->data.val};
	uint32 avail = data.avail;
	if (ctrl->fRegs->control.irqPending && avail > 0) {
		for (uint32 i = 0; i < avail; i++) {
			dprintf(" %02x", data.data);
			data.val = ctrl->fRegs->data.val;
		}
		dprintf("\n");
	}
	return B_HANDLED_INTERRUPT;
}


status_t
AlteraPs2::Read(uint8& val)
{
	//MutexLocker locker(&fLock);
	AlteraPs2Regs::Data data {.val = fRegs->data.val};
	val = data.data;
	return data.avail;
}


status_t
AlteraPs2::Write(uint8 val)
{
	//MutexLocker locker(&fLock);
	fRegs->data.val = AlteraPs2Regs::Data{.data = val}.val;
	AlteraPs2Regs::Control control{.val = fRegs->control.val};
	if (control.error) {
		control.error = false;
		fRegs->control.val = control.val;
		return B_ERROR;
	}
	return B_OK;
}


void
AlteraPs2::SetInterruptHandler(ps2_interrupt_handler handler, void* handlerCookie)
{
	MutexLocker locker(&fLock);
	fInterruptHandler = handler;
	fInterruptCookie = handlerCookie;
}


ps2_device_interface gControllerModuleInfo = {
	{
		{
			.name = PS2_MODULE_NAME,
		},
		.supports_device = [](device_node* parent) {
			return AlteraPs2::SupportsDevice(parent);
		},
		.register_device = [](device_node* parent) {
			return AlteraPs2::RegisterDevice(parent);
		},
		.init_driver = [](device_node* node, void** driverCookie) {
			ObjectDeleter<AlteraPs2> driver(new(std::nothrow) AlteraPs2());
			if (!driver.IsSet()) return B_NO_MEMORY;
			CHECK_RET(driver->InitDriver(node));
			*driverCookie = driver.Detach();
			return B_OK;
		},
		.uninit_driver = [](void* driverCookie) {
			return static_cast<AlteraPs2*>(driverCookie)->UninitDriver();
		},
		.register_child_devices = [](void* driverCookie) {
			return static_cast<AlteraPs2*>(driverCookie)->RegisterChildDevices();
		},
	},
	.read = [](ps2_device cookie, uint8* val) {
		return static_cast<AlteraPs2*>(cookie)->Read(*val);
	},
	.write = [](ps2_device cookie, uint8 val) {
		return static_cast<AlteraPs2*>(cookie)->Write(val);
	},
	.set_interrupt_handler = [](ps2_device cookie, ps2_interrupt_handler handler, void* handlerCookie) {
		return static_cast<AlteraPs2*>(cookie)->SetInterruptHandler(handler, handlerCookie);
	},
};
