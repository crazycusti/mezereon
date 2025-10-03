; stage2_simple.asm - Minimaler Stage 2 Test
BITS 16
ORG 0x7E00

stage2_start:
    ; Stack neu setzen
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Debug: Stage 2 gestartet
    mov al, '2'
    call print_char

    ; Direkt zu Protected Mode ohne A20 oder weitere BIOS-Aufrufe
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

print_char:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    pop bx
    pop ax
    ret

BITS 32
protected_mode:
    ; Segmente setzen
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

    ; VGA Debug Output - sollte "PMOK" anzeigen
    mov ebx, 0xB8000
    mov word [ebx+0], 0x0750     ; 'P' grün auf schwarz
    mov word [ebx+2], 0x074D     ; 'M'
    mov word [ebx+4], 0x074F     ; 'O' 
    mov word [ebx+6], 0x074B     ; 'K'

.halt:
    hlt
    jmp .halt

; GDT
align 4
gdt_start:
    dq 0x0000000000000000   ; Null
    dq 0x00CF9A000000FFFF   ; Code 
    dq 0x00CF92000000FFFF   ; Data
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
