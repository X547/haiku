/*
 * Copyright 2022-2024, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <arch/generic/generic_int.h>
#include <arch/generic/msi.h>
#include <arch_cpu_defs.h>

#include <new>

#include <acpi.h>

#include <dm2/device_manager.h>
#include <dm2/bus/FDT.h>
#include <dm2/bus/ACPI.h>
#include <dm2/device/InterruptController.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDM2.h>
#include <ScopeExit.h>
#include <util/AutoLock.h>
#include <util/Bitmap.h>

#include <cpu.h>
#include <smp.h>

#include <Plic.h>


enum {
	kIselectEidelivery  = 0x70,
	kIselectEithreshold = 0x72,
	kIselectEip0        = 0x80,
	kIselectEip63       = 0xbf,
	kIselectEie0        = 0xc0,
	kIselectEie63       = 0xff,
};


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


//#define TRACE_IMSIC
#ifdef TRACE_IMSIC
#	define TRACE(a...) dprintf("imsic: " a)
#else
#	define TRACE(a...)
#endif
#	define TRACE_ALWAYS(a...) dprintf("imsic: " a)


#define IMSIC_FDT_MODULE_NAME "drivers/interrupt_controllers/imsic/fdt/driver/v1"
#define IMSIC_ACPI_MODULE_NAME "drivers/interrupt_controllers/imsic/acpi/driver/v1"


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


class ImsicInterruptController: public DeviceDriver, public InterruptSource, public MSIInterface {
public:
	virtual ~ImsicInterruptController();

	// DeviceDriver
	static status_t ProbeFdt(DeviceNode* node, DeviceDriver** driver);
	static status_t ProbeAcpi(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// MSIInterface
	status_t AllocateVectors(uint32 count, uint32& startVector, uint64& address, uint32& data) final;
	void FreeVectors(uint32 count, uint32 startVector) final;

	// InterruptSource
	void EnableIoInterrupt(int32 irq) final;
	void DisableIoInterrupt(int32 irq) final;
	void ConfigureIoInterrupt(int32 irq, uint32 config) final {}
	void EndOfInterrupt(int32 irq) final;
	int32 AssignToCpu(int32 irq, int32 cpu) final;

private:
	status_t Init();
	status_t InitFdt(DeviceNode* node);
	status_t InitAcpi(DeviceNode* node);

	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

private:
	spinlock fLock = B_SPINLOCK_INITIALIZER;
	bool fAttached = false;
	int32 fFirstVector {};
	uint32 fIrqCount {};
	phys_addr_t fIrqDestAdrs[SMP_MAX_CPUS] {};
	uint32 fTargetCpus[NUM_IO_VECTORS] {};
	Bitmap fAllocatedVectors;
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
ImsicInterruptController::ProbeFdt(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<ImsicInterruptController> driver(new(std::nothrow) ImsicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitFdt(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
ImsicInterruptController::ProbeAcpi(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<ImsicInterruptController> driver(new(std::nothrow) ImsicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitAcpi(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
ImsicInterruptController::Init()
{
	CHECK_RET(fAllocatedVectors.Resize(fIrqCount));

	CHECK_RET(allocate_io_interrupt_vectors_ex(fIrqCount, &fFirstVector, INTERRUPT_TYPE_IRQ, this));
	CHECK_RET(install_io_interrupt_handler(kHartExternIntVector, HandleInterrupt, this, B_NO_LOCK_VECTOR));
	msi_set_interface(this);
	fAttached = true;

	dprintf("imsic: MSI vector range: %" B_PRIu32 " - %" B_PRIu32 " (%" B_PRIu32 ")\n",
		fFirstVector, fFirstVector + fIrqCount - 1, fIrqCount);

	call_all_cpus_sync([this](int cpu) {
		InterruptsLocker lock;
		SetSiselect(kIselectEidelivery);
		SetSireg(1);
		SetSiselect(kIselectEithreshold);
		SetSireg(fIrqCount + 1);
	});

	return B_OK;
}


status_t
ImsicInterruptController::InitFdt(DeviceNode* node)
{
	TRACE("ImsicInterruptController::InitFdt\n");

	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
	if (fdtDev == NULL)
		return B_ERROR;

	CHECK_RET(fdtDev->GetPropUint32("riscv,num-ids", fIrqCount));

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fdtDev->GetReg(0, &regs, &regsLen))
		return B_ERROR;

	DeviceNode* hartIntcNode;
	uint64 cause;
	bool isModeS = false;
	for (uint32 index = 0; fdtDev->GetInterrupt(index, &hartIntcNode, &cause); index++) {
		DeviceNodePutter hartIntcNodePutter(hartIntcNode);
		DeviceNodePutter hartNode(hartIntcNode->GetParent());
		FdtDevice* hartFdtDev = hartNode->QueryBusInterface<FdtDevice>();

		uint32 hartId;
		CHECK_RET(hartFdtDev->GetPropUint32("reg", hartId));

		TRACE("  index %" B_PRIu32 "\n", index);
		TRACE("    cause: %" B_PRIu64 "\n", cause);
		TRACE("    hartId: %" B_PRIu32 "\n", hartId);

		if (cause == sExternInt) {
			int32 cpu = find_cpu_id_by_hart_id(hartId);
			if (cpu >= 0) {
				isModeS = true;
				fIrqDestAdrs[cpu] = regs + B_PAGE_SIZE * index;
			}
		}
	}

	if (!isModeS)
		return ENODEV;

	return Init();
}


status_t
ImsicInterruptController::InitAcpi(DeviceNode* node)
{
	TRACE("ImsicInterruptController::InitAcpi\n");

	acpi_module_info* acpiModule;
	CHECK_RET(get_module(B_ACPI_MODULE_NAME, (module_info**)&acpiModule));
	ScopeExit acpiModulePutter([]() {
		put_module(B_ACPI_MODULE_NAME);
	});

	acpi_madt* madt;
	CHECK_RET(acpiModule->get_table(ACPI_MADT_SIGNATURE, 0, (void**)&madt));

	enumerate_acpi_madt(madt, [this](acpi_apic* apic) {
		switch (apic->type) {
			case ACPI_MADT_RINTC: {
				acpi_madt_rintc* rintc = (acpi_madt_rintc*)apic;
				if (rintc->version != 1)
					break;

				TRACE("RINTC\n");
				TRACE("  flags: %#" B_PRIx32 "\n", rintc->flags);
				TRACE("  hart_id: %" B_PRIu64 "\n", rintc->hart_id);
				TRACE("  uid: %" B_PRIu32 "\n", rintc->uid);
				TRACE("  ext_intc_id: %" B_PRIu32 "\n", rintc->ext_intc_id);
				TRACE("  imsic_addr: %#" B_PRIx64 "\n", rintc->imsic_addr);
				TRACE("  imsic_size: %#" B_PRIx32 "\n", rintc->imsic_size);

				int32 cpu = find_cpu_id_by_hart_id(rintc->hart_id);
				if (cpu < 0)
					break;

				fIrqDestAdrs[cpu] = rintc->imsic_addr;
				break;
			}
			case ACPI_MADT_IMSIC: {
				acpi_madt_imsic* imsic = (acpi_madt_imsic*)apic;
				if (imsic->version != 1)
					break;

				TRACE("IMSIC\n");
				TRACE("  flags: %#" B_PRIx32 "\n", imsic->flags);
				TRACE("  num_ids: %" B_PRIu16 "\n", imsic->num_ids);
				TRACE("  num_guest_ids: %" B_PRIu16 "\n", imsic->num_guest_ids);
				TRACE("  guest_index_bits: %" B_PRIu8 "\n", imsic->guest_index_bits);
				TRACE("  hart_index_bits: %" B_PRIu8 "\n", imsic->hart_index_bits);
				TRACE("  group_index_bits: %" B_PRIu8 "\n", imsic->group_index_bits);
				TRACE("  group_index_shift: %" B_PRIu8 "\n", imsic->group_index_shift);

				fIrqCount = imsic->num_ids;

				break;
			}
			default:
				break;
		}
	});

	if (fIrqCount == 0)
		return ENODEV;

	return Init();
}


ImsicInterruptController::~ImsicInterruptController()
{
	TRACE("-ImsicInterruptController\n");

	if (fAttached) {
		call_all_cpus_sync([this](int cpu) {
			InterruptsLocker lock;
			SetSiselect(kIselectEidelivery);
			SetSireg(0);
			SetSiselect(kIselectEithreshold);
			SetSireg(0);
		});

		msi_set_interface(NULL);

		remove_io_interrupt_handler(0, HandleInterrupt, this);
		free_io_interrupt_vectors_ex(fIrqCount, fFirstVector);
	}
}


void*
ImsicInterruptController::QueryInterface(const char* name)
{
	if (strcmp(name, MSIInterface::ifaceName) == 0)
		return static_cast<MSIInterface*>(this);

	return NULL;

}


status_t
ImsicInterruptController::AllocateVectors(uint32 count, uint32& outStartVector, uint64& address, uint32& data)
{
	TRACE_ALWAYS("ImsicInterruptController::AllocateVectors(%" B_PRIu32 ")\n", count);
	ssize_t startIndex = fAllocatedVectors.GetLowestContiguousClear(count);
	int32 startVector = startIndex < 0 ? -1 : startIndex + fFirstVector;
	TRACE_ALWAYS("  -> %" B_PRIdSSIZE "\n", startVector);

	if (startVector < 0)
		return ENOENT;

	fAllocatedVectors.SetRange(startIndex, count);

	outStartVector = startVector;
	address = fIrqDestAdrs[fTargetCpus[startIndex]];
	data = startIndex + 1;
	return B_OK;
}


void
ImsicInterruptController::FreeVectors(uint32 count, uint32 startVector)
{
	TRACE_ALWAYS("ImsicInterruptController::FreeVectors(%" B_PRIu8 ", %" B_PRIu8 ")\n", count, startVector);
	fAllocatedVectors.ClearRange(startVector - fFirstVector, count);
}


int32
ImsicInterruptController::HandleInterrupt(void* arg)
{
	return static_cast<ImsicInterruptController*>(arg)->HandleInterruptInt();
}


int32
ImsicInterruptController::HandleInterruptInt()
{
	uint32 irq = (GetAndSetStopei(0) >> 16) % (1 << 11);
	TRACE("ImsicInterruptController::HandleInterrupt(%" B_PRIu32 ")\n", irq);

	// no pending interrupts
	if (irq == 0)
		return B_HANDLED_INTERRUPT;

	int32 vector = irq - 1 + fFirstVector;

	int_io_interrupt_handler(vector, true);
	return B_HANDLED_INTERRUPT;
}


void
ImsicInterruptController::EnableIoInterrupt(int32 vector)
{
	TRACE("ImsicInterruptController::EnableIoInterrupt(%d)\n", irq);
	if (vector < fFirstVector || vector >= fFirstVector + fIrqCount)
		return;

	uint32 irq = vector - fFirstVector + 1;

	call_single_cpu_sync(fTargetCpus[irq - 1], [this, irq](int cpu) {
		InterruptsLocker lock;
		SetSiselect(kIselectEie0 + 2 * (irq / 64));
		SetBitsSireg(1ULL << (irq % 64));
	});
}


void
ImsicInterruptController::DisableIoInterrupt(int32 vector)
{
	TRACE("ImsicInterruptController::DisableIoInterrupt(%d)\n", irq);
	if (vector < fFirstVector || vector >= fFirstVector + fIrqCount)
		return;

	uint32 irq = vector - fFirstVector + 1;

	call_single_cpu_sync(fTargetCpus[irq - 1], [this, irq](int cpu) {
		InterruptsLocker lock;
		SetSiselect(kIselectEie0 + 2 * (irq / 64));
		SetBitsSireg(1ULL << (irq % 64));
	});
}


void
ImsicInterruptController::EndOfInterrupt(int32 vector)
{
	// TODO
}


int32
ImsicInterruptController::AssignToCpu(int32 vector, int32 cpu)
{
	TRACE("ImsicInterruptController::AssignToCpu(%" PRId32 ", %" PRId32 ")\n", vector, cpu);

	uint32 irq = vector - fFirstVector + 1;
	fTargetCpus[irq] = cpu;

	return cpu;
}


static driver_module_info sControllerFdtModuleInfo = {
	.info = {
		.name = IMSIC_FDT_MODULE_NAME,
	},
	.probe = ImsicInterruptController::ProbeFdt
};

static driver_module_info sControllerAcpiModuleInfo = {
	.info = {
		.name = IMSIC_ACPI_MODULE_NAME,
	},
	.probe = ImsicInterruptController::ProbeAcpi
};


_EXPORT module_info* modules[] = {
	(module_info* )&sControllerFdtModuleInfo,
	(module_info* )&sControllerAcpiModuleInfo,
	NULL
};
