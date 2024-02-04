/*
 * Copyright 2023, Haiku, Inc.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <arch/generic/generic_int.h>

#include <new>

#include <dm2/device_manager.h>
#include <dm2/bus/FDT.h>
#include <dm2/device/InterruptController.h>

#include <AutoDeleterOS.h>
#include <AutoDeleterDM2.h>

#include <cpu.h>
#include <smp.h>

#include <Aplic.h>

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


#define APLIC_MODULE_NAME "drivers/interrupt_controllers/aplic/driver/v1"


class AplicInterruptController: public DeviceDriver, public InterruptSource, public InterruptControllerDevice {
public:
	virtual ~AplicInterruptController();

	// DeviceDriver
	static status_t Probe(DeviceNode* node, DeviceDriver** driver);
	void Free() final {delete this;}
	void* QueryInterface(const char* name) final;

	// InterruptControllerDevice
	status_t GetVector(const uint8* optInfo, uint32 optInfoSize, long* vector);

	// InterruptSource
	void EnableIoInterrupt(int irq) final;
	void DisableIoInterrupt(int irq) final;
	void ConfigureIoInterrupt(int irq, uint32 config) final;
	void EndOfInterrupt(int irq) final;
	int32 AssignToCpu(int32 irq, int32 cpu) final;

private:
	status_t Init(DeviceNode* node);

	static int32 HandleInterrupt(void* arg);
	inline int32 HandleInterruptInt();

private:
	AreaDeleter fRegsArea;
	AplicRegs volatile* fRegs {};
	bool fAttached = false;
	uint32 fIrqCount {};
	uint32 fAplicContexts[SMP_MAX_CPUS] {};
	uint32 fPendingContexts[NUM_IO_VECTORS] {};
};


status_t
AplicInterruptController::Probe(DeviceNode* node, DeviceDriver** outDriver)
{
	ObjectDeleter<AplicInterruptController> driver(new(std::nothrow) AplicInterruptController());
	if (!driver.IsSet())
		return B_NO_MEMORY;

	CHECK_RET(driver->Init(node));
	*outDriver = driver.Detach();
	return B_OK;
}


status_t
AplicInterruptController::Init(DeviceNode* node)
{
	dprintf("AplicInterruptController::Init\n");

	FdtDevice* fdtDev = node->QueryBusInterface<FdtDevice>();
	if (fdtDev == NULL)
		return B_ERROR;

	CHECK_RET(fdtDev->GetPropUint32("riscv,num-sources", fIrqCount));
	dprintf("  irqCount: %" B_PRIu32 "\n", fIrqCount);

	int32 cpuCount = smp_get_num_cpus();
	uint32 cookie = 0;
	DeviceNode* hartIntcNode;
	uint64 cause;
	bool isModeS = false;
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
			int32 cpu = 0;
			while (cpu < cpuCount && !(gCPU[cpu].arch.hartId == hartId))
				cpu++;

			if (cpu < cpuCount) {
				isModeS = true;
				fAplicContexts[cpu] = plicContext;
			}
		}
	}

	if (!isModeS)
		return B_DEVICE_NOT_FOUND;

	uint64 regs = 0;
	uint64 regsLen = 0;
	if (!fdtDev->GetReg(0, &regs, &regsLen))
		return B_ERROR;

	fRegsArea.SetTo(map_physical_memory("PLIC MMIO", regs, regsLen, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&fRegs));
	CHECK_RET(fRegsArea.Get());

	reserve_io_interrupt_vectors_ex(fIrqCount + 1, 0, INTERRUPT_TYPE_IRQ, this);
	install_io_interrupt_handler(0, HandleInterrupt, this, B_NO_LOCK_VECTOR);
	fAttached = true;

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


AplicInterruptController::~AplicInterruptController()
{
	dprintf("-AplicInterruptController\n");

	if (fAttached) {
		// TODO: fini hardware

		remove_io_interrupt_handler(0, HandleInterrupt, this);
		free_io_interrupt_vectors_ex(fIrqCount + 1, 0);
	}
}


void*
AplicInterruptController::QueryInterface(const char* name)
{
	if (strcmp(name, InterruptControllerDevice::ifaceName) == 0)
		return static_cast<InterruptControllerDevice*>(this);

	return NULL;

}


status_t
AplicInterruptController::GetVector(const uint8* optInfo, uint32 optInfoSize, long* vector)
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
AplicInterruptController::ConfigureIoInterrupt(int irq, uint32 config)
{
	// TODO: implement
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


static driver_module_info sControllerModuleInfo = {
	.info = {
		.name = APLIC_MODULE_NAME,
	},
	.probe = AplicInterruptController::Probe
};


_EXPORT module_info* modules[] = {
	(module_info* )&sControllerModuleInfo,
	NULL
};
