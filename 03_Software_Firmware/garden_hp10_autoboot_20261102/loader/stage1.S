/* SPDX-License-Identifier: MIT. ROM copies this boot slot to DDR 0x1000. */
.section .init,"ax"
.globl _start
.option norelax
_start:
    csrw mie, zero
    li t0, 8
    csrc mstatus, t0
    /* Verify the bytes copied by the immutable ROM before running them. */
    la t0, stage2_blob
    li t1, STAGE2_BYTES
    li t2, -1
    li t6, 0xEDB88320
1:  lbu t3, 0(t0)
    xor t2, t2, t3
    li t4, 8
2:  andi t5, t2, 1
    srli t2, t2, 1
    beqz t5, 3f
    xor t2, t2, t6
3:  addi t4, t4, -1
    bnez t4, 2b
    addi t0, t0, 1
    addi t1, t1, -1
    bnez t1, 1b
    not t2, t2
    li t3, STAGE2_CRC32
    li a0, 1
    bne t2, t3, fail
    la t0, stage2_blob
    li t1, 0x00C00000
    li t2, STAGE2_BYTES
4:  lbu t3, 0(t0)
    sb t3, 0(t1)
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, -1
    bnez t2, 4b
    fence iorw, iorw
    .word 0x500F /* BSP write-through dcache invalidate */
    fence iorw, iorw
    la t0, stage2_blob
    li t1, 0x00C00000
    li t2, STAGE2_BYTES
5:  lbu t3, 0(t0)
    lbu t4, 0(t1)
    li a0, 2
    bne t3, t4, fail
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, -1
    bnez t2, 5b
    fence iorw, iorw
    .word 0x100F /* BSP instruction cache flush / fence.i */
    li t0, 0x00C00000
    jr t0
fail:
    /* Preserve error in a0 and PC. UART single marker is best effort. */
    li t0, 0xF8010000
    li t1, '!'
    sw t1, 0(t0)
6:  wfi
    j 6b
.balign 4
stage2_blob:
    .incbin "stage2.bin"
stage2_blob_end:
