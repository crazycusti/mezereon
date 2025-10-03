; stage2_final.asm - Stage 2 für neue Ladeadresse 0x10000
BITS 16
ORG 0x10000      ; NEUE ORG-Adresse!

stage2_start:
    ; Segmente und Stack setzen
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

    ; A20 aktivieren (einfach)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Direkt zu Protected Mode
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
    mov esp, 0x90000

    ; VGA Success Output
    mov ebx, 0xB8000
    mov word [ebx+0], 0x4F50    ; 'P' weiß auf rot
    mov word [ebx+2], 0x4F4D    ; 'M'
    mov word [ebx+4], 0x4F4F    ; 'O'
    mov word [ebx+6], 0x4F4B    ; 'K'
    
    ; Zusätzlicher Test: Kernel-Sprung simulieren
    mov word [ebx+10], 0x2F4B   ; 'K' gelb auf grün
    mov word [ebx+12], 0x2F52   ; 'R'
    mov word [ebx+14], 0x2F4E   ; 'N'
    mov word [ebx+16], 0x2F4C   ; 'L'

.halt:
    hlt
    jmp .halt

; GDT muss relativ zur neuen Adresse berechnet werden
align 4
gdt_start:
    dq 0x0000000000000000       ; Null
    dq 0x00CF9A000000FFFF       ; Code 
    dq 0x00CF92000000FFFF       ; Data
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
