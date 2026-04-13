[BITS 32]
%include "boot_shared.inc"
%ifndef DEBUG_ENTRY32
%define DEBUG_ENTRY32 0
%endif
%ifndef ENABLE_BOOTINFO
%define ENABLE_BOOTINFO 1
%endif

%define CODE_SEL 0x08
%define DATA_SEL 0x10
section .text

global _start
extern kentry

_start:
    ; Stage3 sets up a temporary GDT in its own image. The kernel's BSS can overlap
    ; Stage3 memory, so install a kernel-local GDT before we start enabling IRQs.
    cli
    lgdt [gdt_descriptor]
    push dword CODE_SEL
    push dword _start_reload
    retf

_start_reload:
    ; Ensure data segments and stack are sane in protected mode
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; Move stack to 4MB (safe for 12MB Aero, well above 850KB kernel BSS)
    mov esp, 0x400000

%if DEBUG_ENTRY32
    mov ebx, 0xB8000
    mov word [ebx+0], 0x074B   ; 'K'
    mov word [ebx+2], 0x0731   ; '1'
.entry_debug_loop:
    hlt
    jmp .entry_debug_loop
%endif

    ; New arch-neutral entry with optional boot info pointer
%if ENABLE_BOOTINFO
    mov eax, BOOTINFO_ADDR
%else
    xor eax, eax
%endif
    push eax
    call kentry

.halt:
    hlt
    jmp .halt

section .data
align 8
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Mark non-executable stack for GNU ld to suppress warnings
section .note.GNU-stack noalloc noexec nowrite
