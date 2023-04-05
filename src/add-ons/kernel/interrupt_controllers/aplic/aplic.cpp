/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <arch/generic/generic_int.h>
#include <bus/FDT.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDrivers.h>
#include <interrupt_controller.h>

#include <Aplic.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define APLIC_MODULE_NAME "interrupt_controllers/aplic/driver_v1"


static device_manager_info *sDeviceManager;


class AplicInterruptController: public InterruptSource {
private:
	AreaDeleter fRegsArea;
	AplicRegs volatile* fRegs {};
	uint32 fIrqCount {};
	uint32 fAplicContexts[SMP_MAX_CPUS] {};
	uint32 fPendingContexts[NUM_IO_VECTORS] {};

	inline status_t InitDriverInt(device_node* node);

	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

public:
	virtual ~AplicInterruptController() = default;

	static float SupportsDevice(device_node* parent);
	static status_t RegisterDevice(device_node* parent);
	static status_t InitDriver(device_node* node, AplicInterruptController*& driver);
	void UninitDriver();

	status_t GetVector(uint64 irq, long& vector);

	void EnableIoInterrupt(int irq) final;
	void DisableIoInterrupt(int irq) final;
	void ConfigureIoInterrupt(int irq, uint32 config) final {}
	void EndOfInterrupt(int irq) final;
	int32 AssignToCpu(int32 irq, int32 cpu) final;
};


float
AplicInterruptController::SupportsDevice(device_node* parent)
{
	const char* bus;
	status_t status = sDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(bus, "fdt") != 0)
		return 0.0f;

	const char* compatible;
	status = sDeviceManager->get_attr_string(parent, "fdt/compatible", &compatible, false);
	if (status < B_OK)
		return -1.0f;

	if (strcmp(compatible, "riscv,aplic") != 0)
		return 0.0f;

	return 1.0f;
}


status_t
AplicInterruptController::RegisterDevice(device_node* parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {string: "APLIC"} },
		{}
	};

	return sDeviceManager->register_node(parent, APLIC_MODULE_NAME, attrs, NULL, NULL);
}


status_t
AplicInterruptController::InitDriver(device_node* node, AplicInterruptController*& outDriver)
{
	ObjectDeleter<AplicInterruptController> driver(new(std::nothrow) AplicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;
	CHECK_RET(driver->InitDriverInt(node));
	outDriver = driver.Detach();
	return B_OK;
}


status_t
AplicInterruptController::InitDriverInt(device_node* node)
{
	dprintf("AplicInterruptController::InitDriver\n");

	DeviceNodePutter<&sDeviceManager> fdtNode(sDeviceManager->get_parent_node(node));

	const char* bus;
	CHECK_RET(sDeviceManager->get_attr_string(fdtNode.Get(), B_DEVICE_BUS, &bus, false));
	if (strcmp(bus, "fdt") != 0)
		return B_ERROR;

	fdt_device_module_info *fdtModule;
	fdt_device* fdtDev;
	CHECK_RET(sDeviceManager->get_driver(fdtNode.Get(), (driver_module_info**)&fdtModule,
		(void**)&fdtDev));

	const void* prop;
	int propLen;
	prop = fdtModule->get_prop(fdtDev, "riscv,num-sources", &propLen);
	if (prop == NULL || propLen != 4)
		return B_ERROR;

	fIrqCount = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
	dprintf("  irqCount: %" B_PRIu32 "\n", fIrqCount);

	int32 cpuCount = smp_get_num_cpus();
	uint32 cookie = 0;
	device_node* hartIntcNode;
	uint64 cause;
	bool isModeS = false;
	while (fdtModule->get_interrupt(fdtDev, cookie, &hartIntcNode, &cause)) {
		uint32 plicContext = cookie++;
		device_node* hartNode = sDeviceManager->get_parent_node(hartIntcNode);
		DeviceNodePutter<&sDeviceManager> hartNodePutter(hartNode);
		fdt_device* hartDev;
		CHECK_RET(sDeviceManager->get_driver(hartNode, NULL, (void**)&hartDev));
		const void* prop;
		int propLen;
		prop = fdtModule->get_prop(hartDev, "reg", &propLen);
		if (prop == NULL || propLen != 4)
			return B_ERROR;

		uint32 hartId = B_BENDIAN_TO_HOST_INT32(*(const uint32*)prop);
		dprintf("  context %" B_PRIu32 "\n", plicContext);
		dprintf("    cause: %" B_PRIu64 "\n", cause);
		dprintf("    hartId: %" B_PRIu32 "\n", hartId);

		if (cause == sExternInt) {
			int32 cpu = 0;
			while (cpu < cpuCount && !(gCPU[cpu].arch.hartId == hartId))
				cpu++;

			if (cpu < cpuCount) {
				isModeS = true;
				fAplicContexts[cpu] = plicContext;
			}
		}
	}

	if (!isModeS) {
		return B_NO_INIT;
	}

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fdtModule->get_reg(fdtDev, 0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("PLIC MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	reserve_io_interrupt_vectors_ex(fIrqCount + 1, 0, INTERRUPT_TYPE_IRQ, this);
	install_io_interrupt_handler(0, HandleInterrupt, this, B_NO_LOCK_VECTOR);

	fRegs->domainCfg.val = AplicDomainCfg {
		.be = false,
		.dm = AplicDeliveryMode::direct,
		.ie = true
	}.val;

	uint32 context = fAplicContexts[0];
	for (uint32 irq = 1; irq < fIrqCount + 1; irq++) {
		fRegs->sourceCfg[irq].val = AplicSourceCfg {
			.non_deleg = {
				.sm = AplicSourceMode::edge1
			}
		}.val;

		fRegs->target[irq].val = AplicTarget {
			.direct = {
				.iprio = 0,
				.hartIdx = context
			}
		}.val;

	}

	fRegs->idc[context].idelivery = true;
	fRegs->idc[context].ithreshold = 0;

	return B_OK;
}


void
AplicInterruptController::UninitDriver()
{
	dprintf("AplicInterruptController::UninitDriver\n");

	// TODO: fini hardware

	remove_io_interrupt_handler(0, HandleInterrupt, this);
	free_io_interrupt_vectors_ex(fIrqCount + 1, 0);
	delete this;
}


status_t
AplicInterruptController::GetVector(uint64 irq, long& vector)
{
	dprintf("AplicInterruptController::GetVector(%" B_PRIu64 ")\n", irq);
	if (irq < 1 || irq >= fIrqCount + 1)
		return B_BAD_INDEX;

	vector = irq;
	return B_OK;
}


int32
AplicInterruptController::HandleInterrupt(void* arg)
{
	return static_cast<AplicInterruptController*>(arg)->HandleInterruptInt();
}


int32
AplicInterruptController::HandleInterruptInt()
{
	uint32 context = fAplicContexts[smp_get_current_cpu()];

	uint32 irq = fRegs->idc[context].claimi.intNo;
	//dprintf("AplicInterruptController::HandleInterruptInt(context: %" B_PRIu32 ", irq: %" B_PRIu32 ")\n", context, irq);
	if (irq == 0)
		return B_HANDLED_INTERRUPT;

	int_io_interrupt_handler(irq, true);
	return B_HANDLED_INTERRUPT;
}


void
AplicInterruptController::EnableIoInterrupt(int irq)
{
	if (irq == 0)
		return;

	fRegs->setIeNum = irq;
}


void
AplicInterruptController::DisableIoInterrupt(int irq)
{
	if (irq == 0)
		return;

	fRegs->clrIeNum = irq;
}


void
AplicInterruptController::EndOfInterrupt(int irq)
{
	if (irq == 0)
		return;

	// TODO
}


int32
AplicInterruptController::AssignToCpu(int32 irq, int32 cpu)
{
	if (irq == 0)
		return cpu;

	uint32 context = fAplicContexts[cpu];

	fRegs->target[irq].val = AplicTarget {
		.direct = {
			.iprio = 0,
			.hartIdx = context
		}
	}.val;

	return cpu;
}


static interrupt_controller_module_info sControllerModuleInfo = {
	{
		{
			.name = APLIC_MODULE_NAME,
		},
		.supports_device = AplicInterruptController::SupportsDevice,
		.register_device = AplicInterruptController::RegisterDevice,
		.init_driver = [](device_node* node, void** driverCookie) {
			return AplicInterruptController::InitDriver(node,
				*(AplicInterruptController**)driverCookie);
		},
		.uninit_driver = [](void* driverCookie) {
			return static_cast<AplicInterruptController*>(driverCookie)->UninitDriver();
		},
	},
	.get_vector = [](void* cookie, uint64 irq, long* vector) {
		return static_cast<AplicInterruptController*>(cookie)->GetVector(irq, *vector);
	},
};

_EXPORT module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info **)&sDeviceManager },
	{}
};

_EXPORT module_info *modules[] = {
	(module_info *)&sControllerModuleInfo,
	NULL
};
