BITS 32

%include "boot_config.inc"

%define CODE_SEL 0x08
%define DATA_SEL 0x10

section .text

global stage3_entry
extern stage3_main

stage3_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STAGE3_STACK_TOP
    push edi              ; bootinfo pointer
    push esi              ; stage3 params pointer
    call stage3_main
.halt:
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite
