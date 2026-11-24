/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
.syntax unified
.cpu arm926ej-s
.arm
.section .vectors,"ax"
    b _start
    b undefined_trap
    b undefined_trap
    b undefined_trap
    b undefined_trap
    b undefined_trap
    b undefined_trap
    b undefined_trap
.section .text.trap,"ax"
undefined_trap:
    sub r0, lr, #4
    ldr sp, =0x07e00000
    bl trap
1:  b 1b
.section .text.start,"ax"
.global _start
_start:
    ldr sp, =0x07f00000
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
1:  cmp r0, r1
    strlo r2, [r0], #4
    blo 1b
    bl main
2:  b 2b
