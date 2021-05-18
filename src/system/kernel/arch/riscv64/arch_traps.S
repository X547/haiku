# NOTE: this macro don't save SP, it should be saved manually
.macro PushTrapFrame
	addi sp, sp, -256

	sd ra,   0*8(sp)
	sd t6,   1*8(sp)
#	sd sp,   2*8(sp) # sp
	sd gp,   3*8(sp)
	sd tp,   4*8(sp)
	sd t0,   5*8(sp)
	sd t1,   6*8(sp)
	sd t2,   7*8(sp)
	sd t5,   8*8(sp)
	sd s1,   9*8(sp)
	sd a0,  10*8(sp)
	sd a1,  11*8(sp)
	sd a2,  12*8(sp)
	sd a3,  13*8(sp)
	sd a4,  14*8(sp)
	sd a5,  15*8(sp)
	sd a6,  16*8(sp)
	sd a7,  17*8(sp)
	sd s2,  18*8(sp)
	sd s3,  19*8(sp)
	sd s4,  20*8(sp)
	sd s5,  21*8(sp)
	sd s6,  22*8(sp)
	sd s7,  23*8(sp)
	sd s8,  24*8(sp)
	sd s9,  25*8(sp)
	sd s10, 26*8(sp)
	sd s11, 27*8(sp)
	sd t3,  28*8(sp)
	sd t4,  29*8(sp)
	sd fp,  30*8(sp)

	addi fp, sp, 256
.endm


.macro PopTrapFrame
	ld ra,   0*8(sp)
	ld t6,   1*8(sp)
#	ld sp,   2*8(sp) restore later
	ld gp,   3*8(sp)
#	ld tp,   4*8(sp)
	ld t0,   5*8(sp)
	ld t1,   6*8(sp)
	ld t2,   7*8(sp)
	ld t5,   8*8(sp)
	ld s1,   9*8(sp)
	ld a0,  10*8(sp)
	ld a1,  11*8(sp)
	ld a2,  12*8(sp)
	ld a3,  13*8(sp)
	ld a4,  14*8(sp)
	ld a5,  15*8(sp)
	ld a6,  16*8(sp)
	ld a7,  17*8(sp)
	ld s2,  18*8(sp)
	ld s3,  19*8(sp)
	ld s4,  20*8(sp)
	ld s5,  21*8(sp)
	ld s6,  22*8(sp)
	ld s7,  23*8(sp)
	ld s8,  24*8(sp)
	ld s9,  25*8(sp)
	ld s10, 26*8(sp)
	ld s11, 27*8(sp)
	ld t3,  28*8(sp)
	ld t4,  29*8(sp)
	ld fp,  30*8(sp)

	ld sp,   2*8(sp)
.endm


.globl MVec
.type  MVec, @function
.align 4
MVec:
	PushTrapFrame
	sd fp, 2*8(sp)
	csrr t0, mepc
	sd   t0, 31*8(sp)

	mv a0, sp
	call MTrap

	ld t0, 31*8(sp)
	csrw mepc, t0
	PopTrapFrame
	mret
.size	MVec, .-MVec


.globl MVecS
.type  MVecS, @function
.align 4
MVecS:
	csrrw sp, mscratch, sp

	PushTrapFrame

	csrr t0, mscratch
	sd t0, 2*8(sp) # save supervisor SP
	csrw mscratch, fp

	csrr t0, mepc
	sd   t0, 31*8(sp)

	la   t0,    MVec
	csrw mtvec, t0

	mv a0, sp
	call MTrap

	la   t0,    MVecS
	csrw mtvec, t0

	ld t0, 31*8(sp)
	csrw mepc, t0
	PopTrapFrame
	mret
.size	MVecS, .-MVecS


.globl SVec
.type  SVec, @function
.align 4
SVec:
	PushTrapFrame
	sd fp, 2*8(sp)
	csrr t0, sepc
	sd   t0, 31*8(sp)

	mv a0, sp
	call STrap

	ld t0, 31*8(sp)
	csrw sepc, t0
	PopTrapFrame
	sret
.size	SVec, .-SVec


.globl SVecU
.type  SVecU, @function
.align 4
SVecU:
	csrrw t0, sscratch, t0   # t0: &arch_thread
	ld    tp,        0*8(t0) # tp = arch_thread.thread
	ld    t0, (1 + 13)*8(t0) # t0 = arch_thread.context.sp
	sd    sp,  2*8 - 256(t0) # save user SP
	mv    sp, t0             # switch to kernel stack
	csrr  t0, sscratch

	PushTrapFrame

	csrr t0, sepc
	sd   t0, 31*8(sp)

	la   t0,    SVec
	csrw stvec, t0

	mv a0, sp
	call STrap

.globl SVecURet
.type  SVecURet, @function
SVecURet:
	call RestoreUserRegs

	csrr  t0, sscratch
	sd    fp, (1 + 13)*8(t0) # arch_thread.context.sp = fp

	la   t0,    SVecU
	csrw stvec, t0

	ld t0, 31*8(sp)
	csrw sepc, t0
	PopTrapFrame
	sret
.size	SVecU, .-SVecU
