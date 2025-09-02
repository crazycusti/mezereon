! SPARC V8 OBP client entry (sun4m). Not built by default.
! QEMU will pass %o0 = promvec*, %o1 = bootargs (char*), %o2 = openfirmware handle (varies).

        .text
        .globl  _start
        .type   _start, #function
        .extern sparc_boot_main

_start:
        save    %sp, -0x1000, %sp     ! create a simple stack frame
        mov     %i0, %o0              ! pass promvec* as first arg
        call    sparc_boot_main
        nop
1:      ba      1b
        nop

        .size   _start, .-_start

