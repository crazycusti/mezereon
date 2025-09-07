! SPARC V8 OBP client entry (sun4m). Not built by default.
! QEMU will pass %o0 = promvec*, %o1 = bootargs (char*), %o2 = openfirmware handle (varies).

        .text
        .globl  _start
        .type   _start, #function
        .extern sparc_boot_main

        .bss
        .align  8
_boot_stack:
        .skip   8192                   ! 8 KiB stack
_boot_stack_top:

        .text
_start:
        sethi   %hi(_boot_stack_top), %sp
        or      %sp, %lo(_boot_stack_top), %sp   ! set SP to top of our static stack
        andn    %sp, 7, %sp                      ! align stack to 8 bytes
        ! Do NOT save a register window here; let C do its own prologue.
        ! If booted via -kernel, OpenBIOS does NOT pass an OF entry in %o0.
        ! Pass NULL to C in that case to avoid calling a garbage pointer.
        clr     %o0
        call    sparc_boot_main
        nop
1:      ba      1b
        nop

        .size   _start, .-_start
