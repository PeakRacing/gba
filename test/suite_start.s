.syntax unified
.arm
.section .text.start,"ax"
.global _start
_start:
    b boot
    .space 188
boot:
    ldr sp, =0x03007f00
    ldr r0, =__iwram_load
    ldr r1, =__iwram_start
    ldr r2, =__iwram_end
    bl copy
    ldr r0, =__data_load
    ldr r1, =__data_start
    ldr r2, =__data_end
    bl copy
    ldr r0, =__ewram_load
    ldr r1, =__ewram_start
    ldr r2, =__ewram_end
    bl copy
    ldr r1, =__bss_start
    ldr r2, =__bss_end
    mov r0, #0
1:  cmp r1, r2
    strlo r0, [r1], #4
    blo 1b
    ldr r0, =main
    bx r0
copy:
    cmp r1, r2
    ldrlo r3, [r0], #4
    strlo r3, [r1], #4
    blo copy
    bx lr
