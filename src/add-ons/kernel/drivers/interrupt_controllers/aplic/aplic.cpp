/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <arch/generic/generic_int.h>
#include <arch/generic/msi.h>

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

#include <Aplic.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


//#define TRACE_APLIC
#ifdef TRACE_APLIC
#	define TRACE(a...) dprintf("aplic: " a)
#else
#	define TRACE(a...)
#endif
#	define TRACE_ALWAYS(a...) dprintf("aplic: " a)
#	define TRACE_ERROR(a...) dprintf("[!] aplic: " a)


#define APLIC_FDT_MODULE_NAME "drivers/interrupt_controllers/aplic/fdt/driver/v1"
#define APLIC_ACPI_MODULE_NAME "drivers/interrupt_controllers/aplic/acpi/driver/v1"


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


class AplicInterruptController: public DeviceDriver, public InterruptSource, public InterruptControllerDeviceFdt {
public:
	virtual ~AplicInterruptController();

	// DeviceDriver
	static status_t ProbeFdt(DeviceNode* node, DeviceDriver** driver);
	static status_t ProbeAcpi(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// InterruptControllerDeviceFdt
	status_t GetVector(const uint32* intrData, uint32 intrCells, long* vector);

	// InterruptSource
	void EnableIoInterrupt(int vector) final;
	void DisableIoInterrupt(int vector) final;
	void ConfigureIoInterrupt(int vector, uint32 config) final;
	void EndOfInterrupt(int vector) final;
	int32 AssignToCpu(int32 vector, int32 cpu) final;

private:
	status_t Init(uint64 regs, uint64 regsLen);
	status_t InitDirect(uint64 regs, uint64 regsLen);
	status_t InitMsi(uint64 regs, uint64 regsLen, uint64 imsicRegs);
	status_t InitFdt(DeviceNode* node);
	status_t InitAcpi(DeviceNode* node);

	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

	static int32 HandleInterruptMsi(void* arg);
	inline int32 HandleInterruptMsiInt(uint32 irq);

private:
	AreaDeleter fRegsArea;
	AplicRegs volatile* fRegs {};
	bool fAttached = false;
	bool fIsMsi = false;

	MSIInterface* fMsi {};
	long fFirstVector = -1;
	uint32 fMsiVector {};
	uint32 fMsiData {};

	uint32 fIrqCount {};
	uint32 fAplicContexts[SMP_MAX_CPUS] {};
	uint32 fPendingContexts[NUM_IO_VECTORS] {};
	AplicInterruptController* fMsiVectorCookies[NUM_IO_VECTORS] {};
};


status_t
AplicInterruptController::ProbeFdt(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<AplicInterruptController> driver(new(std::nothrow) AplicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitFdt(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
AplicInterruptController::ProbeAcpi(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<AplicInterruptController> driver(new(std::nothrow) AplicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->InitAcpi(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
AplicInterruptController::Init(uint64 regs, uint64 regsLen)
{
	fRegsArea.SetTo(map_physical_memory("APLIC MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	if (fFirstVector < 0)
		allocate_io_interrupt_vectors_ex(fIrqCount, &fFirstVector, INTERRUPT_TYPE_IRQ, this);
	else
		reserve_io_interrupt_vectors_ex(fIrqCount, fFirstVector, INTERRUPT_TYPE_IRQ, this);

	fAttached = true;

	TRACE_ALWAYS("vector range: %" B_PRIu32 " - %" B_PRIu32 " (%" B_PRIu32 ")\n",
		fFirstVector, fFirstVector + fIrqCount - 1, fIrqCount);

	for (uint32 i = 0; i < fIrqCount; i++) {
		uint32 irq = i + 1;
		fRegs->sourceCfg[irq].val = AplicSourceCfg {
			.non_deleg = {
				.sm = AplicSourceMode::edge1
			}
		}.val;
	}

	return B_OK;
}


status_t
AplicInterruptController::InitDirect(uint64 regs, uint64 regsLen)
{
	CHECK_RET(Init(regs, regsLen));

	CHECK_RET(install_io_interrupt_handler(kHartExternIntVector, HandleInterrupt, this, B_NO_LOCK_VECTOR));

	fRegs->domainCfg.val = AplicDomainCfg {
		.be = false,
		.dm = AplicDeliveryMode::direct,
		.ie = true
	}.val;

	return B_OK;
}


status_t
AplicInterruptController::InitMsi(uint64 regs, uint64 regsLen, uint64 imsicRegs)
{
	CHECK_RET(Init(regs, regsLen));

	for (uint32 i = 0; i < fIrqCount; i++)
		fMsiVectorCookies[i] = this;

	uint64 msiAddress;
	CHECK_RET(fMsi->AllocateVectors(fIrqCount, fMsiVector, msiAddress, fMsiData));
	TRACE("fMsiVector: %" B_PRIu32 "\n", fMsiVector);
	TRACE("msiAddress: %#" B_PRIx64 "\n", msiAddress);
	TRACE("fMsiData: %" B_PRIu32 "\n", fMsiData);

	for (uint32 i = 0; i < fIrqCount; i++) {
		CHECK_RET(install_io_interrupt_handler(fMsiVector + i, HandleInterruptMsi, &fMsiVectorCookies[i], B_NO_LOCK_VECTOR));
	}

	fRegs->domainCfg.val = AplicDomainCfg {
		.be = false,
		.dm = AplicDeliveryMode::msi,
		.ie = true
	}.val;

	return B_OK;
}



status_t
AplicInterruptController::InitFdt(DeviceNode* node)
{
	TRACE_ALWAYS("InitFdt\n");

	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
	if (fdtDev == NULL)
		return ENODEV;

	CHECK_RET(fdtDev->GetPropUint32("riscv,num-sources", fIrqCount));

	DeviceNodePutter fdtBusNode(fdtDev->GetBus());
	FdtBus* fdtBus = fdtBusNode->QueryDriverInterface<FdtBus>();

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fdtDev->GetReg(0, &regs, &regsLen))
		return ENODEV;

	uint32 msiParent;
	uint64 imsicRegs = 0;
	uint64 imsicRegsLen = 0;
	if (fdtDev->GetPropUint32("msi-parent", msiParent) >= 0) {
		fIsMsi = true;
		DeviceNodePutter imsicNode(fdtBus->NodeByPhandle(msiParent));
		if (!imsicNode.IsSet())
			return ENODEV;

		FdtDevice* imsicFdtDev = imsicNode->QueryBusInterface<FdtDevice>();

		fMsi = imsicNode->QueryDriverInterface<MSIInterface>();
		if (fMsi == NULL)
			return ENODEV;

		if (!imsicFdtDev->GetReg(0, &imsicRegs, &imsicRegsLen))
			return ENODEV;

		DeviceNode* hartIntcNode;
		uint64 cause;
		bool isModeS = false;
		for (uint32 plicContext = 0; imsicFdtDev->GetInterrupt(plicContext, &hartIntcNode, &cause); plicContext++) {
			DeviceNodePutter hartIntcNodePutter(hartIntcNode);
			DeviceNodePutter hartNode(hartIntcNode->GetParent());
			FdtDevice* hartFdtDev = hartNode->QueryBusInterface<FdtDevice>();

			uint32 hartId;
			CHECK_RET(hartFdtDev->GetPropUint32("reg", hartId));

			TRACE_ALWAYS("  plicContext %" B_PRIu32 "\n", plicContext);
			TRACE_ALWAYS("    cause: %" B_PRIu64 "\n", cause);
			TRACE_ALWAYS("    hartId: %" B_PRIu32 "\n", hartId);

			if (cause == sExternInt) {
				int32 cpu = find_cpu_id_by_hart_id(hartId);
				if (cpu >= 0) {
					isModeS = true;
					fAplicContexts[cpu] = plicContext;
				}
			}
		}

		if (!isModeS)
			return B_DEVICE_NOT_FOUND;

		return InitMsi(regs, regsLen, imsicRegs);
	}

	DeviceNode* hartIntcNode;
	uint64 cause;
	bool isModeS = false;
	for (uint32 plicContext = 0; fdtDev->GetInterrupt(plicContext, &hartIntcNode, &cause); plicContext++) {
		DeviceNodePutter hartIntcNodePutter(hartIntcNode);
		DeviceNodePutter hartNode(hartIntcNode->GetParent());
		FdtDevice* hartFdtDev = hartNode->QueryBusInterface<FdtDevice>();

		uint32 hartId;
		CHECK_RET(hartFdtDev->GetPropUint32("reg", hartId));

		TRACE_ALWAYS("  context %" B_PRIu32 "\n", plicContext);
		TRACE_ALWAYS("    cause: %" B_PRIu64 "\n", cause);
		TRACE_ALWAYS("    hartId: %" B_PRIu32 "\n", hartId);

		if (cause == sExternInt) {
			int32 cpu = find_cpu_id_by_hart_id(hartId);
			if (cpu >= 0) {
				isModeS = true;
				fAplicContexts[cpu] = plicContext;
			}
		}
	}

	if (!isModeS)
		return B_DEVICE_NOT_FOUND;

	return InitDirect(regs, regsLen);
}


status_t
AplicInterruptController::InitAcpi(DeviceNode* node)
{
	TRACE_ALWAYS("InitAcpi\n");

	uint64 regs = 0;
	uint64 regsLen = 0;

	acpi_module_info* acpiModule;
	CHECK_RET(get_module(B_ACPI_MODULE_NAME, (module_info**)&acpiModule));
	ScopeExit acpiModulePutter([]() {
		put_module(B_ACPI_MODULE_NAME);
	});

	acpi_madt* madt;
	CHECK_RET(acpiModule->get_table(ACPI_MADT_SIGNATURE, 0, (void**)&madt));

	bool aplicFound = false;
	uint32 aplicId = 0;

	enumerate_acpi_madt(madt, [this, &regs, &regsLen, &aplicFound, &aplicId](acpi_apic* apic) {
		switch (apic->type) {
			case ACPI_MADT_APLIC: {
				acpi_madt_aplic* aplic = (acpi_madt_aplic*)apic;
				if (aplic->version != 1)
					break;

				if (aplicFound) {
					TRACE_ERROR("multiple APLIC found, using first one\n");
					break;
				}

				aplicFound = true;
				aplicId = aplic->id;
				fFirstVector = aplic->gsi_base;
				fIrqCount = aplic->num_sources;
				regs = aplic->base_addr;
				regsLen = aplic->size;
				break;
			}
			default:
				break;
		}
	});

	if (!aplicFound)
		return ENODEV;

	enumerate_acpi_madt(madt, [this, aplicId](acpi_apic* apic) {
		switch (apic->type) {
			case ACPI_MADT_RINTC:
			{
				acpi_madt_rintc* rintc = (acpi_madt_rintc*)apic;
				if (rintc->version != 1)
					break;

				uint32 hartId = rintc->hart_id;
				uint32 rintcAplicId = (rintc->ext_intc_id >> 24) % (1 << 8);
				uint32 contextId = rintc->ext_intc_id % (1 << 16);

				if (rintcAplicId != aplicId)
					break;

				int32 cpu = find_cpu_id_by_hart_id(hartId);
				if (cpu >= 0)
					fAplicContexts[cpu] = contextId;

				break;
			}
			default:
				break;
		}
	});

	return Init(regs, regsLen);
}


AplicInterruptController::~AplicInterruptController()
{
	TRACE("-AplicInterruptController\n");

	if (fAttached) {
		// TODO: fini hardware

		remove_io_interrupt_handler(0, HandleInterrupt, this);
		free_io_interrupt_vectors_ex(fIrqCount + 1, 0);
	}
}


void*
AplicInterruptController::QueryInterface(const char* name)
{
	if (strcmp(name, InterruptControllerDeviceFdt::ifaceName) == 0)
		return static_cast<InterruptControllerDeviceFdt*>(this);

	return NULL;

}


status_t
AplicInterruptController::GetVector(const uint32* intrData, uint32 intrCells, long* vector)
{
	if (intrCells != 1 && intrCells != 2)
		return B_BAD_VALUE;

	uint32 irq = B_BENDIAN_TO_HOST_INT32(intrData[0]);
#if 0
	if (intrCells >= 2) {
		AplicSourceMode sourceMode = (AplicSourceMode)B_BENDIAN_TO_HOST_INT32(intrData[1]);
	}
#endif

	if (irq < 1 || irq >= fIrqCount + 1)
		return B_BAD_INDEX;

	*vector = irq - 1 + fFirstVector;
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
	TRACE("AplicInterruptController::HandleInterruptInt(context: %" B_PRIu32 ", irq: %" B_PRIu32 ")\n", context, irq);
	if (irq == 0)
		return B_HANDLED_INTERRUPT;

	long vector = irq - 1 + fFirstVector;

	int_io_interrupt_handler(vector, true);
	return B_HANDLED_INTERRUPT;
}


int32
AplicInterruptController::HandleInterruptMsi(void* arg)
{
	auto cookie = static_cast<AplicInterruptController**>(arg);
	AplicInterruptController* self = *cookie;
	uint32 irq = cookie - self->fMsiVectorCookies + 1;
	return self->HandleInterruptMsiInt(irq);
}


int32
AplicInterruptController::HandleInterruptMsiInt(uint32 irq)
{
	TRACE("HandleInterruptMsiInt(irq: %" B_PRIu32 ")\n", irq);

	long vector = irq - 1 + fFirstVector;
	int_io_interrupt_handler(vector, true);
	//fRegs->setIpNum = irq;
	return B_HANDLED_INTERRUPT;
}


void
AplicInterruptController::EnableIoInterrupt(int vector)
{
	uint32 irq = vector - fFirstVector + 1;
	fRegs->setIeNum = irq;
}


void
AplicInterruptController::DisableIoInterrupt(int vector)
{
	uint32 irq = vector - fFirstVector + 1;
	fRegs->clrIeNum = irq;
}


void
AplicInterruptController::ConfigureIoInterrupt(int vector, uint32 config)
{
	// TODO: implement
}


void
AplicInterruptController::EndOfInterrupt(int irq)
{
	// TODO: implement
}


int32
AplicInterruptController::AssignToCpu(int32 vector, int32 cpu)
{
	TRACE_ALWAYS("AssignToCpu(%" B_PRId32 ", %" B_PRId32 ")\n", vector, cpu);

	uint32 irq = vector - fFirstVector + 1;
	uint32 context = fAplicContexts[cpu];

	fRegs->idc[context].idelivery = true;
	fRegs->idc[context].ithreshold = 0;

	if (fIsMsi) {
		fRegs->target[irq].val = AplicTarget {
			.msi = {
				.eiid = fMsiData + (irq - 1),
				.hartIdx = context
			}
		}.val;
	} else {
		fRegs->target[irq].val = AplicTarget {
			.direct = {
				.iprio = 0,
				.hartIdx = context
			}
		}.val;
	}

	return cpu;
}


static driver_module_info sControllerFdtModuleInfo = {
	.info = {
		.name = APLIC_FDT_MODULE_NAME,
	},
	.probe = AplicInterruptController::ProbeFdt
};

static driver_module_info sControllerAcpiModuleInfo = {
	.info = {
		.name = APLIC_ACPI_MODULE_NAME,
	},
	.probe = AplicInterruptController::ProbeAcpi
};


_EXPORT module_info* modules[] = {
	(module_info*)&sControllerFdtModuleInfo,
	(module_info*)&sControllerAcpiModuleInfo,
	NULL
};
