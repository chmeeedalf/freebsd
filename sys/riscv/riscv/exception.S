/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

#include "assym.s"

#include <machine/trap.h>
#include <machine/riscvreg.h>

.macro save_registers el
	addi	sp, sp, -280

	sd	ra, (TF_RA)(sp)
	sd	gp, (TF_GP)(sp)
	sd	tp, (TF_TP)(sp)

	sd	t0, (TF_T + 0 * 8)(sp)
	sd	t1, (TF_T + 1 * 8)(sp)
	sd	t2, (TF_T + 2 * 8)(sp)
	sd	t3, (TF_T + 3 * 8)(sp)
	sd	t4, (TF_T + 4 * 8)(sp)
	sd	t5, (TF_T + 5 * 8)(sp)
	sd	t6, (TF_T + 6 * 8)(sp)

	sd	s0, (TF_S + 0 * 8)(sp)
	sd	s1, (TF_S + 1 * 8)(sp)
	sd	s2, (TF_S + 2 * 8)(sp)
	sd	s3, (TF_S + 3 * 8)(sp)
	sd	s4, (TF_S + 4 * 8)(sp)
	sd	s5, (TF_S + 5 * 8)(sp)
	sd	s6, (TF_S + 6 * 8)(sp)
	sd	s7, (TF_S + 7 * 8)(sp)
	sd	s8, (TF_S + 8 * 8)(sp)
	sd	s9, (TF_S + 9 * 8)(sp)
	sd	s10, (TF_S + 10 * 8)(sp)
	sd	s11, (TF_S + 11 * 8)(sp)

	sd	a0, (TF_A + 0 * 8)(sp)
	sd	a1, (TF_A + 1 * 8)(sp)
	sd	a2, (TF_A + 2 * 8)(sp)
	sd	a3, (TF_A + 3 * 8)(sp)
	sd	a4, (TF_A + 4 * 8)(sp)
	sd	a5, (TF_A + 5 * 8)(sp)
	sd	a6, (TF_A + 6 * 8)(sp)
	sd	a7, (TF_A + 7 * 8)(sp)

#if 0
	/* XXX: temporary test: spin if stack is not kernel one */
.if \el == 1	/* kernel */
	mv	t0, sp
	srli	t0, t0, 63
1:
	beqz	t0, 1b
.endif
#endif

.if \el == 1
	/* Store kernel sp */
	sd	sp, (TF_SP)(sp)
.else
	/* Store user sp */
	csrr	t0, sscratch
	sd	t0, (TF_SP)(sp)
.endif
	li	t0, 0
	csrw	sscratch, t0
	csrr	t0, sepc
	sd	t0, (TF_SEPC)(sp)
	csrr	t0, sstatus
	sd	t0, (TF_SSTATUS)(sp)
	csrr	t0, sbadaddr
	sd	t0, (TF_SBADADDR)(sp)
	csrr	t0, scause
	sd	t0, (TF_SCAUSE)(sp)
.endm

.macro load_registers el
	ld	t0, (TF_SSTATUS)(sp)
.if \el == 0
	/* Ensure user interrupts will be enabled on eret. */
	ori	t0, t0, SSTATUS_PIE
.else
	/*
	 * Disable interrupts for supervisor mode exceptions.
	 * For user mode exceptions we have already done this
	 * in do_ast.
	 */
	li	t1, ~SSTATUS_IE
	and	t0, t0, t1
.endif
	csrw	sstatus, t0

	ld	t0, (TF_SEPC)(sp)
	csrw	sepc, t0

.if \el == 0
	/* Load user sp */
	ld	t0, (TF_SP)(sp)
	csrw	sscratch, t0
.endif

	ld	ra, (TF_RA)(sp)
	ld	gp, (TF_GP)(sp)
	ld	tp, (TF_TP)(sp)

	ld	t0, (TF_T + 0 * 8)(sp)
	ld	t1, (TF_T + 1 * 8)(sp)
	ld	t2, (TF_T + 2 * 8)(sp)
	ld	t3, (TF_T + 3 * 8)(sp)
	ld	t4, (TF_T + 4 * 8)(sp)
	ld	t5, (TF_T + 5 * 8)(sp)
	ld	t6, (TF_T + 6 * 8)(sp)

	ld	s0, (TF_S + 0 * 8)(sp)
	ld	s1, (TF_S + 1 * 8)(sp)
	ld	s2, (TF_S + 2 * 8)(sp)
	ld	s3, (TF_S + 3 * 8)(sp)
	ld	s4, (TF_S + 4 * 8)(sp)
	ld	s5, (TF_S + 5 * 8)(sp)
	ld	s6, (TF_S + 6 * 8)(sp)
	ld	s7, (TF_S + 7 * 8)(sp)
	ld	s8, (TF_S + 8 * 8)(sp)
	ld	s9, (TF_S + 9 * 8)(sp)
	ld	s10, (TF_S + 10 * 8)(sp)
	ld	s11, (TF_S + 11 * 8)(sp)

	ld	a0, (TF_A + 0 * 8)(sp)
	ld	a1, (TF_A + 1 * 8)(sp)
	ld	a2, (TF_A + 2 * 8)(sp)
	ld	a3, (TF_A + 3 * 8)(sp)
	ld	a4, (TF_A + 4 * 8)(sp)
	ld	a5, (TF_A + 5 * 8)(sp)
	ld	a6, (TF_A + 6 * 8)(sp)
	ld	a7, (TF_A + 7 * 8)(sp)

	addi	sp, sp, 280
.endm

.macro	do_ast
	/* Disable interrupts */
	csrr	a4, sstatus
1:
	csrci	sstatus, SSTATUS_IE

	la	a1, pcpup
	ld	a1, 0(a1)
	ld	a1, PC_CURTHREAD(a1)
	lw	a2, TD_FLAGS(a1)

	li	a3, (TDF_ASTPENDING|TDF_NEEDRESCHED)
	and	a2, a2, a3
	beqz	a2, 2f

	/* Restore interrupts */
	andi	a4, a4, SSTATUS_IE
	csrs	sstatus, a4

	/* Handle the ast */
	mv	a0, sp
	call	_C_LABEL(ast)

	/* Re-check for new ast scheduled */
	j	1b
2:
.endm

ENTRY(cpu_exception_handler_supervisor)
	save_registers 1
	mv	a0, sp
	call	_C_LABEL(do_trap_supervisor)
	load_registers 1
	eret
END(cpu_exception_handler_supervisor)

ENTRY(cpu_exception_handler_user)
	csrrw	sp, sscratch, sp
	save_registers 0
	mv	a0, sp
	call	_C_LABEL(do_trap_user)
	do_ast
	load_registers 0
	csrrw	sp, sscratch, sp
	eret
END(cpu_exception_handler_user)

/*
 * Trap handlers
 */
	.text
bad_trap:
	j bad_trap

user_trap:
	csrrw	sp, mscratch, sp
	addi	sp, sp, -64
	sd	t0, (8 * 0)(sp)
	sd	t1, (8 * 1)(sp)
	sd	t2, (8 * 2)(sp)
	sd	t3, (8 * 3)(sp)
	sd	t4, (8 * 4)(sp)
	sd	t5, (8 * 5)(sp)
	sd	a0, (8 * 7)(sp)

	la	t2, _C_LABEL(cpu_exception_handler_user)

	csrr    t0, mcause
	bltz    t0, machine_interrupt
	j	exit_mrts

supervisor_trap:
	/* Save state */
	csrrw	sp, mscratch, sp
	addi	sp, sp, -64
	sd	t0, (8 * 0)(sp)
	sd	t1, (8 * 1)(sp)
	sd	t2, (8 * 2)(sp)
	sd	t3, (8 * 3)(sp)
	sd	t4, (8 * 4)(sp)
	sd	t5, (8 * 5)(sp)
	sd	a0, (8 * 7)(sp)

	la	t2, _C_LABEL(cpu_exception_handler_supervisor)

	csrr	t0, mcause
	bltz	t0, machine_interrupt

	li	t1, EXCP_SMODE_ENV_CALL
	beq	t0, t1, supervisor_call
	j	exit_mrts

machine_interrupt:
	/* Type of interrupt ? */
	csrr	t0, mcause
	andi	t0, t0, 3
	li	t1, 0
	beq	t1, t0, software_interrupt
	li	t1, 1
	beq	t1, t0, timer_interrupt
	li	t1, 2
	beq	t1, t0, htif_interrupt

	/* not reached */
1:
	j	1b

software_interrupt:
	/* Redirect to supervisor */
	j	exit_mrts

timer_interrupt:
	/* Disable machine timer interrupts */
	li	t0, MIE_MTIE
	csrc	mie, t0

	/* Clear machine pending */
	li	t0, MIP_MTIP
	csrc	mip, t0

	/* Post supervisor software interrupt */
	li	t0, MIP_STIP
	csrs	mip, t0

	/* If PRV1 is PRV_U (user) then serve a trap */
	csrr	t0, mstatus
	li	t1, (MSTATUS_PRV_M << MSTATUS_PRV1_SHIFT)
	and	t0, t0, t1
	beqz	t0, 1f

	/* If PRV1 is supervisor and interrupts were enabled, then serve a trap */
	csrr	t0, mstatus
	li	t1, (SR_IE1 | (MSTATUS_PRV_M << MSTATUS_PRV1_SHIFT))
	and	t0, t0, t1
	li	t1, (SR_IE1 | (MSTATUS_PRV_S << MSTATUS_PRV1_SHIFT))
	beq	t0, t1, 1f

	j	exit

1:
	/* Serve a trap in supervisor mode */
	j	exit_mrts

htif_interrupt:
1:
	li	t5, 0
	csrrw	t5, mfromhost, t5
	beqz	t5, 3f

	/* Console PUT intr ? */
	mv	t1, t5
	li	t0, 0x101
	srli	t1, t1, 48
	bne	t1, t0, 2f
	/* Yes */
	la	t0, console_intr
	li	t1, 1
	sd	t1, 0(t0)
	j	3f

2:
	/* Save entry */
	la	t0, htif_ring_cursor
	beqz	t0, 3f		/* not initialized */
	ld	t0, 0(t0)	/* load struct */
	sd	t5, 0(t0)	/* put entry */
	li	t4, 1
	sd	t4, 8(t0)	/* mark used */
	ld	t4, 16(t0)	/* take next */
	/* Update cursor */
	la	t0, htif_ring_cursor
	sd	t4, 0(t0)

	/* Post supervisor software interrupt */
	li	t0, MIP_SSIP
	csrs	mip, t0

3:
	j	exit

supervisor_call:
	csrr	t1, mepc
	addi	t1, t1, 4	/* Next instruction in t1 */
	li	t4, ECALL_HTIF_CMD
	beq	t5, t4, htif_cmd
	li	t4, ECALL_HTIF_GET_ENTRY
	beq	t5, t4, htif_get_entry
	li	t4, ECALL_MTIMECMP
	beq	t5, t4, set_mtimecmp
	li	t4, ECALL_CLEAR_PENDING
	beq	t5, t4, clear_pending
	li	t4, ECALL_MCPUID_GET
	beq	t5, t4, mcpuid_get
	li	t4, ECALL_MIMPID_GET
	beq	t5, t4, mimpid_get
	j	exit_next_instr

mcpuid_get:
	csrr	t6, mcpuid
	j	exit_next_instr

mimpid_get:
	csrr	t6, mimpid
	j	exit_next_instr

htif_get_entry:
	li	t6, 0		/* preset return value */
	la	t0, htif_ring_last
	ld	t0, 0(t0)	/* load struct */
	ld	t4, 8(t0)	/* get used */
	beqz	t4, 1f
	ld	t6, 0(t0)	/* get entry */
	li	t4, 0
	sd	t4, 8(t0)	/* mark free */
	sd	t4, 0(t0)	/* free entry, just in case */
	ld	t4, 16(t0)	/* take next */
	la	t0, htif_ring_last
	sd	t4, 0(t0)
1:
	/* Exit. Result is stored in t6 */
	j	exit_next_instr

htif_cmd:
	mv	t0, t6
1:
	csrrw	t0, mtohost, t0
	bnez	t0, 1b
	j	exit_next_instr

set_mtimecmp:
	csrr	t2, stime
	add	t6, t6, t2
	csrw	mtimecmp, t6

	/* Enable interrupts */
	li	t0, (MIE_MTIE | MIE_STIE)
	csrs	mie, t0
	j	exit_next_instr

clear_pending:
	li      t0, MIP_STIP
	csrc    mip, t0
	j	exit_next_instr

/*
 * Trap exit functions
 */
exit_next_instr:
	/* Next instruction is in t1 */
	csrw    mepc, t1
exit:
	/* Restore state */
	ld	t0, (8 * 0)(sp)
	ld	t1, (8 * 1)(sp)
	ld	t2, (8 * 2)(sp)
	ld	t3, (8 * 3)(sp)
	ld	t4, (8 * 4)(sp)
	ld	t5, (8 * 5)(sp)
	ld	a0, (8 * 7)(sp)
	addi	sp, sp, 64
	csrrw	sp, mscratch, sp
	eret

/*
 * Redirect to supervisor
 */
exit_mrts:
	/* Setup exception handler */
	li	t1, KERNBASE
	add	t2, t2, t1
	csrw	stvec, t2

	/* Restore state */
	ld	t0, (8 * 0)(sp)
	ld	t1, (8 * 1)(sp)
	ld	t2, (8 * 2)(sp)
	ld	t3, (8 * 3)(sp)
	ld	t4, (8 * 4)(sp)
	ld	t5, (8 * 5)(sp)
	ld	a0, (8 * 7)(sp)
	addi	sp, sp, 64
	csrrw	sp, mscratch, sp

	/* Redirect to supervisor */
	mrts
