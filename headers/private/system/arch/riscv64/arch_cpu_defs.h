/*
 * Copyright 2018-2019, Haiku, Inc. All rights reserved
 * Distributed under the terms of the MIT License.
 * 
 * Authors:
 * 	Alexander von Gluck IV <kallisti5@unixzen.com>
 */
#ifndef _SYSTEM_ARCH_RISCV64_DEFS_H
#define _SYSTEM_ARCH_RISCV64_DEFS_H


enum {
	modeU = 0,
	modeS = 1,
	modeM = 3,
};

// fs, xs
enum {
	extStatusOff     = 0,
	extStatusInitial = 1,
	extStatusClean   = 2,
	extStatusDirty   = 3,
};

struct MstatusReg {
	union {
		struct {
			uint64_t ie:      4; // interrupt enable
			uint64_t pie:     4; // previous interrupt enable
			uint64_t spp:     1; // previous mode (supervisor)
			uint64_t unused1: 2;
			uint64_t mpp:     2; // previous mode (machine)
			uint64_t fs:      2; // FPU status
			uint64_t xs:      2; // extensions status
			uint64_t mprv:    1; // modify privelege
			uint64_t sum:     1; // permit supervisor user memory access
			uint64_t mxr:     1; // make executable readable
			uint64_t tvm:     1; // trap virtual memory
			uint64_t tw:      1; // timeout wait (trap WFI)
			uint64_t tsr:     1; // trap SRET
			uint64_t unused2: 9;
			uint64_t uxl:     2; // U-mode XLEN
			uint64_t sxl:     2; // S-mode XLEN
			uint64_t unused3: 27;
			uint64_t sd:      1; // status dirty
		};
		uint64_t val;
	};
	
	MstatusReg() {}
	MstatusReg(uint64_t val): val(val) {}
};

struct SstatusReg {
	union {
		struct {
			uint64_t ie:      2; // interrupt enable
			uint64_t unused1: 2;
			uint64_t pie:     2; // previous interrupt enable
			uint64_t unused2: 2;
			uint64_t spp:     1; // previous mode (supervisor)
			uint64_t unused3: 4;
			uint64_t fs:      2; // FPU status
			uint64_t xs:      2; // extensions status
			uint64_t unused4: 1;
			uint64_t sum:     1; // permit supervisor user memory access
			uint64_t mxr:     1; // make executable readable
			uint64_t unused5: 12;
			uint64_t uxl:     2; // U-mode XLEN
			uint64_t unused6: 29;
			uint64_t sd:      1; // status dirty
		};
		uint64_t val;
	};
	
	SstatusReg() {}
	SstatusReg(uint64_t val): val(val) {}
};

enum {
	softInt    = 0,
	uSoftInt   = softInt + modeU,
	sSoftInt   = softInt + modeS,
	mSoftInt   = softInt + modeM,
	timerInt   = 4,
	uTimerInt  = timerInt + modeU,
	sTimerInt  = timerInt + modeS,
	mTimerInt  = timerInt + modeM,
	externInt  = 8,
	uExternInt = externInt + modeU,
	sExternInt = externInt + modeS,
	mExternInt = externInt + modeM,
};

enum {
	causeInterrupt        = 1ULL << 63, // rest bits are interrupt number
	causeExecMisalign     = 0,
	causeExecAccessFault  = 1,
	causeIllegalInst      = 2,
	causeBreakpoint       = 3,
	causeLoadMisalign     = 4,
	causeLoadAccessFault  = 5,
	causeStoreMisalign    = 6,
	causeStoreAccessFault = 7,
	causeECall            = 8,
	causeUEcall           = causeECall + modeU,
	causeSEcall           = causeECall + modeS,
	causeMEcall           = causeECall + modeM,
	causeExecPageFault    = 12,
	causeLoadPageFault    = 13,
	causeStorePageFault   = 15,
};

enum {
	pageSize = 4096,
	pageBits = 12,
	pteCount = 512,
	pteIdxBits = 9,
};

enum {
	pteValid    = 0,
	pteRead     = 1,
	pteWrite    = 2,
	pteExec     = 3,
	pteUser     = 4,
	pteGlobal   = 5,
	pteAccessed = 6,
	pteDirty    = 7,
};

struct Pte {
	union {
		struct {
			uint64_t flags:     8;
			uint64_t rsw:       2;
			uint64_t ppn:      44;
			uint64_t reserved: 10;
		};
		uint64_t val;
	};
	
	Pte() {}
	Pte(uint64_t val): val(val) {}
};

enum {
	satpModeBare =  0,
	satpModeSv39 =  8,
	satpModeSv48 =  9,
	satpModeSv57 = 10,
	satpModeSv64 = 11,
};

struct SatpReg {
	union {
		struct {
			uint64_t ppn:  44;
			uint64_t asid: 16;
			uint64_t mode:  4;
		};
		uint64_t val;
	};
	
	SatpReg() {}
	SatpReg(uint64_t val): val(val) {}
};

static ALWAYS_INLINE uint64_t PhysAdrPte(uint64_t physAdr, uint32_t level)
{
	return (physAdr >> (pageBits + pteIdxBits*level)) % (1 << pteIdxBits);
}

static ALWAYS_INLINE uint64_t PhysAdrOfs(uint64_t physAdr)
{
	return physAdr % pageSize;
}

// CPU core ID
static ALWAYS_INLINE uint64_t Mhartid() {uint64_t x; asm volatile("csrr %0, mhartid" : "=r" (x)); return x;}

// status register
static ALWAYS_INLINE uint64_t Mstatus() {uint64_t x; asm volatile("csrr %0, mstatus" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMstatus(uint64_t x) {asm volatile("csrw mstatus, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Sstatus() {uint64_t x; asm volatile("csrr %0, sstatus" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSstatus(uint64_t x) {asm volatile("csrw sstatus, %0" : : "r" (x));}

// exception program counter
static ALWAYS_INLINE uint64_t Mepc() {uint64_t x; asm volatile("csrr %0, mepc" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMepc(uint64_t x) {asm volatile("csrw mepc, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Sepc() {uint64_t x; asm volatile("csrr %0, sepc" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSepc(uint64_t x) {asm volatile("csrw sepc, %0" : : "r" (x));}

// interrupt pending
static ALWAYS_INLINE uint64_t Mip() {uint64_t x; asm volatile("csrr %0, mip" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMip(uint64_t x) {asm volatile("csrw mip, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Sip() {uint64_t x; asm volatile("csrr %0, sip" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSip(uint64_t x) {asm volatile("csrw sip, %0" : : "r" (x));}

// interrupt enable
static ALWAYS_INLINE uint64_t Sie() {uint64_t x; asm volatile("csrr %0, sie" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSie(uint64_t x) {asm volatile("csrw sie, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Mie() {uint64_t x; asm volatile("csrr %0, mie" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMie(uint64_t x) {asm volatile("csrw mie, %0" : : "r" (x));}

// exception delegation
static ALWAYS_INLINE uint64_t Medeleg() {uint64_t x; asm volatile("csrr %0, medeleg" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMedeleg(uint64_t x) {asm volatile("csrw medeleg, %0" : : "r" (x));}
// interrupt delegation
static ALWAYS_INLINE uint64_t Mideleg() {uint64_t x; asm volatile("csrr %0, mideleg" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMideleg(uint64_t x) {asm volatile("csrw mideleg, %0" : : "r" (x));}

// trap vector, 2 low bits: mode
static ALWAYS_INLINE uint64_t Mtvec() {uint64_t x; asm volatile("csrr %0, mtvec" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMtvec(uint64_t x) {asm volatile("csrw mtvec, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Stvec() {uint64_t x; asm volatile("csrr %0, stvec" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetStvec(uint64_t x) {asm volatile("csrw stvec, %0" : : "r" (x));}

// address translation and protection (pointer to page table and flags)
static ALWAYS_INLINE uint64_t Satp() {uint64_t x; asm volatile("csrr %0, satp" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSatp(uint64_t x) {asm volatile("csrw satp, %0" : : "r" (x));}

// scratch register
static ALWAYS_INLINE uint64_t Mscratch() {uint64_t x; asm volatile("csrr %0, mscratch" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMscratch(uint64_t x) {asm volatile("csrw mscratch, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Sscratch() {uint64_t x; asm volatile("csrr %0, sscratch" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSscratch(uint64_t x) {asm volatile("csrw sscratch, %0" : : "r" (x));}

// trap cause
static ALWAYS_INLINE uint64_t Mcause() {uint64_t x; asm volatile("csrr %0, mcause" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMcause(uint64_t x) {asm volatile("csrw mcause, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Scause() {uint64_t x; asm volatile("csrr %0, scause" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetScause(uint64_t x) {asm volatile("csrw scause, %0" : : "r" (x));}

// trap value
static ALWAYS_INLINE uint64_t Mtval() {uint64_t x; asm volatile("csrr %0, mtval" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMtval(uint64_t x) {asm volatile("csrw mtval, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Stval() {uint64_t x; asm volatile("csrr %0, stval" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetStval(uint64_t x) {asm volatile("csrw stval, %0" : : "r" (x));}

// machine-mode counter-enable
static ALWAYS_INLINE uint64_t Mcounteren() {uint64_t x; asm volatile("csrr %0, mcounteren" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetMcounteren(uint64_t x) {asm volatile("csrw mcounteren, %0" : : "r" (x));}

// machine-mode cycle counter
static ALWAYS_INLINE uint64_t CpuTime() {uint64_t x; asm volatile("csrr %0, time" : "=r" (x)); return x;}

// flush the TLB
static ALWAYS_INLINE void FlushTlbAll() {asm volatile("sfence.vma" : : : "memory");}
static ALWAYS_INLINE void FlushTlbPage(uint64_t x) {asm volatile("sfence.vma %0" : : "r" (x) : "memory");}

static ALWAYS_INLINE bool DisableIntr() {MstatusReg status; status.val = Mstatus(); status.ie &= ~(1 << modeM); SetMstatus(status.val); return (status.ie & (1 << modeM)) != 0;}
static ALWAYS_INLINE void RestoreIntr(bool enable) {if (enable) {MstatusReg status; status.val = Mstatus(); status.ie |= (1 << modeM); SetMstatus(status.val);}}
static ALWAYS_INLINE void EnableIntr() {MstatusReg status; status.val = Mstatus(); status.ie |= (1 << modeM); SetMstatus(status.val);}
static ALWAYS_INLINE bool IsIntrEnabled() {MstatusReg status; status.val = Mstatus(); return (status.ie & (1 << modeM)) != 0;}

static ALWAYS_INLINE uint64_t Sp() {uint64_t x; asm volatile("mv %0, sp" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetSp(uint64_t x) {asm volatile("mv sp, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Fp() {uint64_t x; asm volatile("mv %0, fp" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetFp(uint64_t x) {asm volatile("mv fp, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Tp() {uint64_t x; asm volatile("mv %0, tp" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetTp(uint64_t x) {asm volatile("mv tp, %0" : : "r" (x));}
static ALWAYS_INLINE uint64_t Ra() {uint64_t x; asm volatile("mv %0, ra" : "=r" (x)); return x;}
static ALWAYS_INLINE void SetRa(uint64_t x) {asm volatile("mv ra, %0" : : "r" (x));}

static ALWAYS_INLINE void Ecall() {asm volatile("ecall");}

// Wait for interrupts, reduce CPU load when inactive.
static ALWAYS_INLINE void Wfi() {asm volatile("wfi");}

static ALWAYS_INLINE __attribute__ ((noreturn)) void Mret() {asm volatile("mret"); __builtin_unreachable();}
static ALWAYS_INLINE __attribute__ ((noreturn)) void Sret() {asm volatile("sret"); __builtin_unreachable();}


#define SPINLOCK_PAUSE()	do {} while (false)


#endif	/* _SYSTEM_ARCH_RISCV64_DEFS_H */

