[BITS 32]
section .text

global irq0_stub
global irq1_stub
global irq3_stub
global nmi_stub
global page_fault_stub
extern irq0_handler_c
extern irq1_handler_c
extern irq3_handler_c
extern nmi_handler_c
extern page_fault_handler_c

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

; Mark non-executable stack for GNU ld to suppress warnings

nmi_stub:
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
    call nmi_handler_c
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iretd

page_fault_stub:
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
    mov eax, [esp + 48]
    mov ebx, [esp + 52]
    push ebx
    push eax
    call page_fault_handler_c
    add esp, 8
    pop gs
    pop fs
    pop es
    pop ds
    popa
    add esp, 4
    iretd

section .note.GNU-stack noalloc noexec nowrite
