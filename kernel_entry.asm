[BITS 16]

cli
jmp $
lgdt [gdt_descriptor]

mov eax, cr0
or eax, 1
mov cr0, eax

jmp 0x08:protected_mode_start

gdt_start:
    dq 0x0000000000000000     ; Null-Deskriptor
    dq 0x00cf9a000000ffff     ; Code-Segment
    dq 0x00cf92000000ffff     ; Data-Segment
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[BITS 32]
protected_mode_start:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000         ; Stack setzen

    mov dword [0xb8000], 0x1F411F41
    jmp $

    extern kmain
    ;call kmain              
    ;hlt                      
