BITS 32

%include "boot_config.inc"

%define CODE_SEL 0x08
%define DATA_SEL 0x10

extern stage3_main

%if STAGE3_VERBOSE_DEBUG
section .text
send_debug_e9:
    push ax
    push dx
    mov dx, 0xE9
    mov al, bl
    out dx, al
    pop dx
    pop ax
    ret
%endif

section .data
align 8
stage3_gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
stage3_gdt_end:

stage3_gdt_descriptor:
    dw stage3_gdt_end - stage3_gdt_start - 1
    dd stage3_gdt_start

section .text.stage3_entry

global stage3_entry

stage3_entry:
    mov eax, 0x33534721
    xor eax, eax
    mov eax, stage3_gdt_start
    add eax, STAGE3_LINEAR_ADDR
    mov edx, stage3_gdt_descriptor
    add edx, STAGE3_LINEAR_ADDR
    mov [edx + 2], eax
    lgdt [edx]
    mov eax, stage3_entry_reload
    add eax, STAGE3_LINEAR_ADDR
    push dword CODE_SEL
    push eax
    retf

stage3_entry_reload:
%if STAGE3_VERBOSE_DEBUG
    push bx
    mov bl, 'P'
    call send_debug_e9
    pop bx
%endif
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STAGE3_STACK_TOP
%if STAGE3_VERBOSE_DEBUG
    mov word [0xB8000], 0x0753
    mov word [0xB8002], 0x0745
%endif
    push edi              ; bootinfo pointer
    push esi              ; stage3 params pointer
    call stage3_main
.halt:
    hlt
    jmp .halt

section .note.GNU-stack noalloc noexec nowrite
