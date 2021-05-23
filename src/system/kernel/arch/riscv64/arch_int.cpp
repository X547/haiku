/*
 * Copyright 2003-2011, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *      Adrien Destugues, pulkomandy@pulkomandy.tk
 */


#include <int.h>
#include <cpu.h>
#include <thread.h>
#include <vm/vm_priv.h>
#include <ksyscalls.h>
#include <arch_cpu_defs.h>
#include <arch_thread_types.h>
#include <arch/debug.h>
#include <util/AutoLock.h>
#include <Htif.h>
#include <Plic.h>
#include <Clint.h>

#include <algorithm>


__attribute__ ((aligned (16))) char sMStack[64*1024];


extern "C" void MVec();
extern "C" void MVecS();
extern "C" void SVec();
extern "C" void SVecU();


//#pragma mark debug output

void WriteMode(int mode)
{ 
	switch (mode) {
	case modeU: dprintf("u"); break;
	case modeS: dprintf("s"); break;
	case modeM: dprintf("m"); break;
	default: dprintf("%d", mode);
	}
}

void WriteModeSet(uint32_t val)
{
	bool first = true;
	dprintf("{");
	for (int i = 0; i < 32; i++) {
		if (((1LL << i) & val) != 0) {
			if (first) first = false; else dprintf(", ");
			WriteMode(i);
		}
	}
	dprintf("}");
}

void WriteMstatus(uint64_t val)
{
	MstatusReg status(val);
	dprintf("(");
	dprintf("ie: "); WriteModeSet(status.ie);
	dprintf(", pie: "); WriteModeSet(status.pie);
	dprintf(", spp: "); WriteMode(status.spp);
	dprintf(", mpp: "); WriteMode(status.mpp);
	dprintf(", sum: %d", (int)status.sum);
	dprintf(")");
}

void WriteSstatus(uint64_t val)
{
	SstatusReg status(val);
	dprintf("(");
	dprintf("ie: "); WriteModeSet(status.ie);
	dprintf(", pie: "); WriteModeSet(status.pie);
	dprintf(", spp: "); WriteMode(status.spp);
	dprintf(", sum: %d", (int)status.sum);
	dprintf(")");
}

void WriteInterrupt(uint64_t val)
{
	switch (val) {
	case 0 + modeU: dprintf("uSoft"); break;
	case 0 + modeS: dprintf("sSoft"); break;
	case 0 + modeM: dprintf("mSoft"); break;
	case 4 + modeU: dprintf("uTimer"); break;
	case 4 + modeS: dprintf("sTimer"); break;
	case 4 + modeM: dprintf("mTimer"); break;
	case 8 + modeU: dprintf("uExtern"); break;
	case 8 + modeS: dprintf("sExtern"); break;
	case 8 + modeM: dprintf("mExtern"); break;
	default: dprintf("%" B_PRId64, val);
	}
}

void WriteInterruptSet(uint64_t val)
{
	bool first = true;
	dprintf("{");
	for (int i = 0; i < 64; i++) {
		if (((1LL << i) & val) != 0) {
			if (first) first = false; else dprintf(", ");
			WriteInterrupt(i);
		}
	}
	dprintf("}");
}

void WriteCause(uint64_t cause)
{
	if ((cause >> 63) == 0) {
		dprintf("exception ");
		switch (cause) {
			case causeExecMisalign: dprintf("execMisalign"); break;
			case causeExecAccessFault: dprintf("execAccessFault"); break;
			case causeIllegalInst: dprintf("illegalInst"); break;
			case causeBreakpoint: dprintf("breakpoint"); break;
			case causeLoadMisalign: dprintf("loadMisalign"); break;
			case causeLoadAccessFault: dprintf("loadAccessFault"); break;
			case causeStoreMisalign: dprintf("storeMisalign"); break;
			case causeStoreAccessFault: dprintf("storeAccessFault"); break;
			case causeUEcall: dprintf("uEcall"); break;
			case causeSEcall: dprintf("sEcall"); break;
			case causeMEcall: dprintf("mEcall"); break;
			case causeExecPageFault: dprintf("execPageFault"); break;
			case causeLoadPageFault: dprintf("loadPageFault"); break;
			case causeStorePageFault: dprintf("storePageFault"); break;
			default: dprintf("%" B_PRId64, cause);
			}
	} else {
		dprintf("interrupt "); WriteInterrupt(cause & ~(1LL << 63));
	}
}

void WriteTrapInfo()
{
	dprintf("STrap("); WriteCause(Scause()); dprintf(")\n");
	dprintf("  sstatus: "); WriteSstatus(Sstatus()); dprintf("\n");
	dprintf("  sie: "); WriteInterruptSet(Sie()); dprintf("\n");
	dprintf("  sip: "); WriteInterruptSet(Sip()); dprintf("\n");
	dprintf("  stval: "); WritePC(Stval()); dprintf("\n");
	dprintf("  tp: 0x%" B_PRIxADDR "(%s)\n", Tp(), thread_get_current_thread()->name);
	//dprintf("  stval: 0x%" B_PRIx64 "\n", Stval());
}


//#pragma mark -

extern "C" void MTrap(iframe* frame)
{
	uint64 cause = Mcause();
/*
	HtifOutString("+MTrap("); WriteCause(Mcause()); HtifOutString(")\n");
	dprintf("  mstatus: "); WriteMstatus(Mstatus()); dprintf("\n");
	dprintf("  mie: "); WriteInterruptSet(Mie()); dprintf("\n");
	dprintf("  mip: "); WriteInterruptSet(Mip()); dprintf("\n");
	dprintf("  sie: "); WriteInterruptSet(Sie()); dprintf("\n");
	dprintf("  sip: "); WriteInterruptSet(Sip()); dprintf("\n");
	dprintf("  mscratch: 0x%" B_PRIxADDR "\n", Mscratch());
	DoStackTrace(Fp(), 0);
*/
	switch (cause) {
	case causeMEcall:
	case causeSEcall: {
		frame->epc += 4;
		uint64 op = frame->a0;
		switch (op) {
		case switchToSmodeMmodeSyscall: {
			HtifOutString("switchToSmodeMmodeSyscall()\n");
			MstatusReg status(Mstatus());
			status.mpp = modeS;
			SetMedeleg(0xffff & ~((1 << causeMEcall) | (1 << causeSEcall)));
			SetMideleg(0xffff & ~(1 << mTimerInt));
			SetMstatus(status.val);
			dprintf("modeM stack: 0x%" B_PRIxADDR ", 0x%" B_PRIxADDR "\n", (addr_t)sMStack, (addr_t)(sMStack + sizeof(sMStack)));
			SetMscratch((addr_t)(sMStack + sizeof(sMStack)));
			SetMtvec((uint64)MVecS);
			frame->a0 = B_OK;
			return;
		}
		case setTimerMmodeSyscall: {
			// HtifOutString("setTimerMmodeSyscall()\n");
			bool enable = frame->a1 != 0;
			SetSip(Sip() & ~(1 << sTimerInt));
			if (!enable) {
				SetMie(Mie() & ~(1 << mTimerInt));
			} else {
				gClintRegs->mTimeCmp[0] = frame->a2;
				SetMie(Mie() | (1 << mTimerInt));
			}
			frame->a0 = B_OK;
			return;
		}
		default:
			frame->a0 = B_NOT_SUPPORTED;
			return;
		}
		break;
	}
	case causeInterrupt + mTimerInt: {
		disable_interrupts();
		SetMie(Mie() & ~(1 << mTimerInt));
		SetMip(Mip() | (1 << sTimerInt));
		return;
	}
	}
	HtifOutString("unhandled MTrap\n");
	// DoStackTrace(Fp(), 0);
	HtifShutdown();
}


static void SendSignal(debug_exception_type type, uint32 signalNumber, int32 signalCode, addr_t signalAddress = 0, int32 signalError = B_ERROR)
{
	if (SstatusReg(Sstatus()).spp == modeU) {
		struct sigaction action;
		Thread* thread = thread_get_current_thread();

		WriteTrapInfo();
		DoStackTrace(Fp(), 0);

		enable_interrupts();

		// If the thread has a signal handler for the signal, we simply send it
		// the signal. Otherwise we notify the user debugger first.
		if ((sigaction(signalNumber, NULL, &action) == 0
				&& action.sa_handler != SIG_DFL
				&& action.sa_handler != SIG_IGN)
			|| user_debug_exception_occurred(type, signalNumber)) {
			Signal signal(signalNumber, signalCode, signalError,
				thread->team->id);
			signal.SetAddress((void*)signalAddress);
			send_signal_to_thread(thread, signal, 0);
		}
	} else {
		WriteTrapInfo();
		panic("Unexpected exception occurred in kernel mode!");
	}
}

static void AfterInterrupt()
{
	Thread* thread = thread_get_current_thread();
	cpu_status state = disable_interrupts();
	if (thread->cpu->invoke_scheduler) {
		SpinLocker schedulerLocker(thread->scheduler_lock);
		scheduler_reschedule(B_THREAD_READY);
		schedulerLocker.Unlock();
		restore_interrupts(state);
	} else if (thread->post_interrupt_callback != NULL) {
		void (*callback)(void*) = thread->post_interrupt_callback;
		void* data = thread->post_interrupt_data;

		thread->post_interrupt_callback = NULL;
		thread->post_interrupt_data = NULL;

		restore_interrupts(state);

		callback(data);
	}
}


extern "C" void STrap(iframe* frame)
{
	// dprintf("STrap("); WriteCause(Scause()); dprintf(")\n");
	if (SstatusReg(Sstatus()).spp == modeU) {
		thread_get_current_thread()->arch_info.userFrame = frame;
		thread_at_kernel_entry(system_time());
	}
	struct ScopeExit {
		~ScopeExit()
		{
			if (SstatusReg(Sstatus()).spp == modeU) {
				if ((thread_get_current_thread()->flags & (THREAD_FLAGS_SIGNALS_PENDING | THREAD_FLAGS_DEBUG_THREAD | THREAD_FLAGS_TRAP_FOR_CORE_DUMP)) != 0) {
					enable_interrupts();
					thread_at_kernel_exit();
				} else {
					thread_at_kernel_exit_no_signals();
				}
				thread_get_current_thread()->arch_info.userFrame = NULL;
			}
		}
	} scopeExit;

	uint64 cause = Scause();
	switch (cause) {
	case causeIllegalInst:
		return SendSignal(B_INVALID_OPCODE_EXCEPTION, SIGILL, ILL_ILLOPC, frame->epc);
	case causeExecMisalign:
	case causeLoadMisalign:
	case causeStoreMisalign:
		return SendSignal(B_ALIGNMENT_EXCEPTION, SIGBUS, BUS_ADRALN, Stval());
	// case causeExecAccessFault:
	// case causeBreakpoint:
	// case causeLoadAccessFault:
	// case causeStoreAccessFault:
	case causeUEcall: {
		frame->epc += 4;
		uint64 syscall = frame->t0;
		uint64 args[20];
		if (syscall < (uint64)kSyscallCount) {
			uint32 argCnt = kExtendedSyscallInfos[syscall].parameter_count;
			memcpy(&args[0], &frame->a0, sizeof(uint64)*std::min<uint32>(argCnt, 8));
			if (argCnt > 8) {
				if(user_memcpy(&args[8], (void*)frame->sp, sizeof(uint64)*(argCnt - 8)) < B_OK) {
					dprintf("can't read syscall arguments on user stack\n");
					frame->a0 = B_BAD_ADDRESS;
					return;
				}
			}
		}
		// dprintf("syscall: %s\n", kExtendedSyscallInfos[syscall].name);
		SstatusReg status(Sstatus());
		enable_interrupts();
		uint64 returnValue = 0;
		syscall_dispatcher(syscall, (void*)args, &returnValue);
		frame->a0 = returnValue;
		SetSstatus(status.val);
		return;
	}
	case causeExecPageFault:
	case causeLoadPageFault:
	case causeStorePageFault: {
		uint64 stval = Stval();
		SstatusReg status(Sstatus());
		addr_t newIP = 0;
		enable_interrupts();
		vm_page_fault(stval, frame->epc, cause == causeStorePageFault, cause == causeExecPageFault, status.spp == modeU, &newIP);
		if (newIP != 0)
			frame->epc = newIP;
		SetSstatus(status.val);
		return;
	}
	case causeInterrupt + sTimerInt: {
		SstatusReg status(Sstatus());
		timer_interrupt();
		AfterInterrupt();
		SetSstatus(status.val);
		return;
	}
	case causeInterrupt + sExternInt:
		SstatusReg status(Sstatus());
		uint64 irq = gPlicRegs->contexts[0].claimAndComplete;
		int_io_interrupt_handler(irq, true);
		gPlicRegs->contexts[0].claimAndComplete = irq;
		AfterInterrupt();
		SetSstatus(status.val);
		return;
	}
	WriteTrapInfo();
	panic("unhandled STrap");
}


status_t
arch_int_init(kernel_args *args)
{
	SetMtvec((uint64)MVec);
	SetStvec((uint64)SVec);
	MstatusReg mstatus(Mstatus());
	mstatus.ie = 1 << modeM;
	mstatus.fs = extStatusInitial; // enable FPU
	mstatus.xs = extStatusOff;
	SetMstatus(mstatus.val);
	MSyscall(switchToSmodeMmodeSyscall);
	SetSie(Sie() | (1 << sTimerInt) | (1 << sExternInt));

	// TODO: read from FDT
	reserve_io_interrupt_vectors(32, 0, INTERRUPT_TYPE_IRQ);
	
	return B_OK;
}


status_t
arch_int_init_post_vm(kernel_args *args)
{
	return B_OK;
}


status_t
arch_int_init_post_device_manager(struct kernel_args *args)
{
	return B_OK;
}


status_t
arch_int_init_io(kernel_args* args)
{
	return B_OK;
}


void
arch_int_enable_io_interrupt(int irq)
{
}


void
arch_int_disable_io_interrupt(int irq)
{
}


void
arch_int_assign_to_cpu(int32 irq, int32 cpu)
{
}
