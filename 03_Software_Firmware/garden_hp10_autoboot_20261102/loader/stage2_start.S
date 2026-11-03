/* SPDX-License-Identifier: MIT. No dependence on low DDR after entry. */
.section .init,"ax"
.globl _start
.option norelax
_start:
    csrw mie, zero
    li t0, 8
    csrc mstatus, t0
    la gp, __global_pointer$
    la sp, __stack_top
    la t0, __bss_start
    la t1, __bss_end
1:  bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b
2:  la t0, trap_handler
    csrw mtvec, t0
    call boot_main
3:  wfi
    j 3b
.align 2
trap_handler:
    csrr a0, mcause
    csrr a1, mepc
    tail boot_trap
