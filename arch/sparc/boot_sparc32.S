! SPARC V8 OBP client entry (sun4m). Not built by default.
! QEMU will pass %o0 = promvec*, %o1 = bootargs (char*), %o2 = openfirmware handle (varies).

    .section .text,"ax",@progbits
    .align 4
    .globl  _start
_start:
    sethi   %hi(_boot_stack_top), %sp
    or      %sp, %lo(_boot_stack_top), %sp
    andn    %sp, 7, %sp
    clr     %o0
    call    sparc_boot_main
    nop
1:  ba      1b
    nop

    .section .bss,"aw",@nobits
    .align 8
_boot_stack:
        .skip   8192                   ! 8 KiB stack
_boot_stack_top:

        .text
