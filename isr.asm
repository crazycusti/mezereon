[BITS 32]

global irq0_stub
extern irq0_handler_c

irq0_stub:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call irq0_handler_c
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

