; stage2_ultra_simple.asm - Kein BIOS, direkt zu Protected Mode
BITS 16
ORG 0x7E00

stage2_start:
    ; Keine BIOS-Aufrufe! Direkt zu Protected Mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

BITS 32  
protected_mode:
    ; Segmente setzen
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000    ; Stack setzen
    
    ; VGA Output
    mov ebx, 0xB8000
    mov word [ebx+0], 0x2A50    ; 'P' gelb auf grün - sehr auffällig
    mov word [ebx+2], 0x2A4D    ; 'M'
    mov word [ebx+4], 0x2A4F    ; 'O'
    mov word [ebx+6], 0x2A4B    ; 'K'

.halt:
    hlt
    jmp .halt

; GDT
align 4
gdt_start:
    dq 0x0000000000000000       ; Null Descriptor
    dq 0x00CF9A000000FFFF       ; Code Segment
    dq 0x00CF92000000FFFF       ; Data Segment  
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
