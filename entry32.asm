[BITS 32]

global _start
extern kmain

_start:
    ; Ensure data segments and stack are sane in protected mode
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

    call kmain

.halt:
    hlt
    jmp .halt

