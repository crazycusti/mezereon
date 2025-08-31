[BITS 32]

global irq0_stub
global irq1_stub
global irq3_stub
extern irq0_handler_c
extern irq1_handler_c
extern irq3_handler_c

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

irq1_stub:
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
    call irq1_handler_c
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

irq3_stub:
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
    call irq3_handler_c
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd
