[BITS 32]

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

    ; New arch-neutral entry with optional boot info pointer (NULL for now)
    push dword 0
    call kentry

.halt:
    hlt
    jmp .halt

; Mark non-executable stack for GNU ld to suppress warnings
section .note.GNU-stack noalloc noexec nowrite
