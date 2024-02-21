/*
 * Copyright 2022, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <arch/generic/generic_int.h>

#include <new>

#include <acpi.h>

#include <dm2/device_manager.h>
#include <dm2/bus/FDT.h>
#include <dm2/bus/ACPI.h>
#include <dm2/device/InterruptController.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDM2.h>
#include <ScopeExit.h>

#include <cpu.h>
#include <smp.h>

#include <Plic.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define PLIC_FDT_MODULE_NAME "drivers/interrupt_controllers/plic/fdt/driver/v1"
#define PLIC_ACPI_MODULE_NAME "drivers/interrupt_controllers/plic/acpi/driver/v1"


struct acpi_madt_rintc {
    acpi_apic               header;
    uint8                   version;
    uint8                   reserved;
    uint32                  flags;
    uint64                  hart_id;
    uint32                  uid;
    uint32                  ext_intc_id;
    uint64                  imsic_addr;
    uint32                  imsic_size;
};

/* Values for RISC-V INTC Version field above */

enum AcpiMadtRintcVersion {
    ACPI_MADT_RINTC_VERSION_NONE       = 0,
    ACPI_MADT_RINTC_VERSION_V1         = 1,
    ACPI_MADT_RINTC_VERSION_RESERVED   = 2	/* 2 and greater are reserved */
};

struct acpi_madt_imsic {
    acpi_apic               header;
    uint8                   version;
    uint8                   reserved;
    uint32                  flags;
    uint16                  num_ids;
    uint16                  num_guest_ids;
    uint8                   guest_index_bits;
    uint8                   hart_index_bits;
    uint8                   group_index_bits;
    uint8                   group_index_shift;
};

struct acpi_madt_aplic {
    acpi_apic               header;
    uint8                   version;
    uint8                   id;
    uint32                  flags;
    uint8                   hw_id[8];
    uint16                  num_idcs;
    uint16                  num_sources;
    uint32                  gsi_base;
    uint64                  base_addr;
    uint32                  size;
};

struct acpi_madt_plic {
    acpi_apic               header;
    uint8                   version;
    uint8                   id;
    uint8                   hw_id[8];
    uint16                  num_irqs;
    uint16                  max_prio;
    uint32                  flags;
    uint32                  size;
    uint64                  base_addr;
    uint32                  gsi_base;
};


class PlicInterruptController: public DeviceDriver, public InterruptSource, public InterruptControllerDevice {
public:
	virtual ~PlicInterruptController();

	// DeviceDriver
	static status_t ProbeFdt(DeviceNode* node, DeviceDriver** driver);
	static status_t ProbeAcpi(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// InterruptControllerDevice
	status_t GetVector(const uint8* optInfo, uint32 optInfoSize, long* vector) final;

	// InterruptSource
	void EnableIoInterrupt(int irq) final;
	void DisableIoInterrupt(int irq) final;
	void ConfigureIoInterrupt(int irq, uint32 config) final {}
	void EndOfInterrupt(int irq) final;
	int32 AssignToCpu(int32 irq, int32 cpu) final;

private:
	status_t Init(uint64 regs, uint64 regsLen);
	status_t InitFdt(DeviceNode* node);
	status_t InitAcpi(DeviceNode* node);

	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

private:
	AreaDeleter fRegsArea;
	PlicRegs volatile* fRegs {};
	bool fAttached = false;
	uint32 fIrqCount {};
	uint32 fPlicContexts[SMP_MAX_CPUS] {};
	uint32 fPendingContexts[NUM_IO_VECTORS] {};
};


int32
find_cpu_id_by_hart_id(uint32 hartId)
{
	int32 cpuCount = smp_get_num_cpus();

	for (int32 cpu = 0; cpu < cpuCount; cpu++) {
		if (gCPU[cpu].arch.hartId == hartId)
			return cpu;
	}

	return -1;
}


template<typename Callback> static void
enumerate_acpi_madt(acpi_madt* madt, Callback&& cb)
{
	acpi_apic* apic = (acpi_apic*)((uint8 *)madt + sizeof(acpi_madt));
	acpi_apic* apicEnd = (acpi_apic*)((uint8 *)madt + madt->header.length);

	while (apic < apicEnd) {
		cb(apic);
		apic = (acpi_apic *)((uint8 *)apic + apic->length);
	}
}


status_t
PlicInterruptController::ProbeFdt(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<PlicInterruptController> driver(new(std::nothrow) PlicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitFdt(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
PlicInterruptController::ProbeAcpi(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<PlicInterruptController> driver(new(std::nothrow) PlicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitAcpi(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
PlicInterruptController::Init(uint64 regs, uint64 regsLen)
{
	int32 cpuCount = smp_get_num_cpus();

	dprintf("  irqCount: %" B_PRIu32 "\n", fIrqCount);

	fRegsArea.SetTo(map_physical_memory("PLIC MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	reserve_io_interrupt_vectors_ex(fIrqCount + 1, 0, INTERRUPT_TYPE_IRQ, this);
	install_io_interrupt_handler(0, HandleInterrupt, this, B_NO_LOCK_VECTOR);
	fAttached = true;

	for (int32 cpu = 0; cpu < cpuCount; cpu++)
		fRegs->contexts[fPlicContexts[cpu]].priorityThreshold = 0;

	// unmask interrupts
	for (uint32 irq = 1; irq < fIrqCount + 1; irq++)
		fRegs->priority[irq] = 1;

	return B_OK;
}


status_t
PlicInterruptController::InitFdt(DeviceNode* node)
{
	dprintf("PlicInterruptController::InitFdt\n");

	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
	if (fdtDev == NULL)
		return B_ERROR;

	CHECK_RET(fdtDev->GetPropUint32("riscv,ndev", fIrqCount));

	uint32 cookie = 0;
	DeviceNode* hartIntcNode;
	uint64 cause;
	while (fdtDev->GetInterrupt(cookie, &hartIntcNode, &cause)) {
		uint32 plicContext = cookie++;

		DeviceNodePutter hartIntcNodePutter(hartIntcNode);
		DeviceNodePutter hartNode(hartIntcNode->GetParent());
		FdtDevice* hartFdtDev = hartNode->QueryBusInterface<FdtDevice>();

		uint32 hartId;
		CHECK_RET(hartFdtDev->GetPropUint32("reg", hartId));

		dprintf("  context %" B_PRIu32 "\n", plicContext);
		dprintf("    cause: %" B_PRIu64 "\n", cause);
		dprintf("    hartId: %" B_PRIu32 "\n", hartId);

		if (cause == sExternInt) {
			int32 cpu = find_cpu_id_by_hart_id(hartId);
			if (cpu >= 0)
				fPlicContexts[cpu] = plicContext;
		}
	}

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fdtDev->GetReg(0, &regs, &regsLen))
		return B_ERROR;

	return Init(regs, regsLen);
}


status_t
PlicInterruptController::InitAcpi(DeviceNode* node)
{
	dprintf("PlicInterruptController::InitAcpi\n");

	uint64 regs = 0;
	uint64 regsLen = 0;

	acpi_module_info* acpiModule;
	CHECK_RET(get_module(B_ACPI_MODULE_NAME, (module_info**)&acpiModule));
	ScopeExit acpiModulePutter([]() {
		put_module(B_ACPI_MODULE_NAME);
	});

	acpi_madt* madt;
	CHECK_RET(acpiModule->get_table(ACPI_MADT_SIGNATURE, 0, (void**)&madt));

	bool plicFound = false;
	uint32 plicId = 0;

	enumerate_acpi_madt(madt, [this, &regs, &regsLen, &plicFound, &plicId](acpi_apic* apic) {
		switch (apic->type) {
			case ACPI_MADT_PLIC: {
				acpi_madt_plic* plic = (acpi_madt_plic*)apic;
				if (plic->version != 1)
					break;

				if (plicFound) {
					dprintf("[!] plic: multiple PLIC found, using first one\n");
					break;
				}

				plicFound = true;
				plicId = plic->id;
				fIrqCount = plic->num_irqs;
				regs = plic->base_addr;
				regsLen = plic->size;
				break;
			}
			default:
				break;
		}
	});

	if (!plicFound)
		return ENODEV;

	enumerate_acpi_madt(madt, [this, plicId](acpi_apic* apic) {
		switch (apic->type) {
			case ACPI_MADT_RINTC:
			{
				acpi_madt_rintc* rintc = (acpi_madt_rintc*)apic;
				if (rintc->version != 1)
					break;

				uint32 hartId = rintc->hart_id;
				uint32 rintcPlicId = (rintc->ext_intc_id >> 24) % (1 << 8);
				uint32 contextId = rintc->ext_intc_id % (1 << 16);

				if (rintcPlicId != plicId)
					break;

				int32 cpu = find_cpu_id_by_hart_id(hartId);
				if (cpu >= 0)
					fPlicContexts[cpu] = contextId;

				break;
			}
			default:
				break;
		}
	});

	return Init(regs, regsLen);
}


PlicInterruptController::~PlicInterruptController()
{
	dprintf("-PlicInterruptController\n");

	if (fAttached) {
		// mask interrupts
		for (uint32 irq = 1; irq < fIrqCount + 1; irq++)
			fRegs->priority[irq] = 0;

		remove_io_interrupt_handler(0, HandleInterrupt, this);
		free_io_interrupt_vectors_ex(fIrqCount + 1, 0);
	}
}


void*
PlicInterruptController::QueryInterface(const char* name)
{
	if (strcmp(name, InterruptControllerDevice::ifaceName) == 0)
		return static_cast<InterruptControllerDevice*>(this);

	return NULL;

}


status_t
PlicInterruptController::GetVector(const uint8* optInfo, uint32 optInfoSize, long* vector)
{
	if (optInfoSize != 4)
		return B_BAD_VALUE;

	uint32 irq = B_BENDIAN_TO_HOST_INT32(*((const uint32*)optInfo));

	if (irq < 1 || irq >= fIrqCount + 1)
		return B_BAD_INDEX;

	*vector = irq;
	return B_OK;
}


int32
PlicInterruptController::HandleInterrupt(void* arg)
{
	return static_cast<PlicInterruptController*>(arg)->HandleInterruptInt();
}


int32
PlicInterruptController::HandleInterruptInt()
{
	uint32 context = fPlicContexts[smp_get_current_cpu()];
	uint64 irq = fRegs->contexts[context].claimAndComplete;
	if (irq == 0)
		return B_HANDLED_INTERRUPT;
	fPendingContexts[irq] = context;
	int_io_interrupt_handler(irq, true);
	return B_HANDLED_INTERRUPT;
}


void
PlicInterruptController::EnableIoInterrupt(int irq)
{
	if (irq == 0)
		return;

	fRegs->enable[fPlicContexts[0]][irq / 32] |= 1 << (irq % 32);
}


void
PlicInterruptController::DisableIoInterrupt(int irq)
{
	if (irq == 0)
		return;

	fRegs->enable[fPlicContexts[0]][irq / 32] &= ~(1 << (irq % 32));
}


void
PlicInterruptController::EndOfInterrupt(int irq)
{
	if (irq == 0)
		return;

	uint32 context = fPendingContexts[irq];
	fRegs->contexts[context].claimAndComplete = irq;
}


int32
PlicInterruptController::AssignToCpu(int32 irq, int32 cpu)
{
	// Not yet supported.
	return 0;
}


static driver_module_info sControllerFdtModuleInfo = {
	.info = {
		.name = PLIC_FDT_MODULE_NAME,
	},
	.probe = PlicInterruptController::ProbeFdt
};

static driver_module_info sControllerAcpiModuleInfo = {
	.info = {
		.name = PLIC_ACPI_MODULE_NAME,
	},
	.probe = PlicInterruptController::ProbeAcpi
};


_EXPORT module_info* modules[] = {
	(module_info* )&sControllerFdtModuleInfo,
	(module_info* )&sControllerAcpiModuleInfo,
	NULL
};
