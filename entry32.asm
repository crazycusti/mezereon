[BITS 32]
%define BOOTINFO_ADDR 0x00005000
%ifndef DEBUG_ENTRY32
%define DEBUG_ENTRY32 0
%endif
%ifndef ENABLE_BOOTINFO
%define ENABLE_BOOTINFO 1
%endif
section .text

global _start
extern kentry

_start:
    ; Ensure data segments and stack are sane in protected mode
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

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

; Mark non-executable stack for GNU ld to suppress warnings
section .note.GNU-stack noalloc noexec nowrite
