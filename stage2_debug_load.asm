; stage2_debug_load.asm - Debug Version mit begrenztem Loading
BITS 16
ORG 0x10000

%include "boot_shared.inc"

; Fixed limits für Debug
%define DEBUG_BOOT 1
%define KERNEL_SECTORS_LIMIT 50    ; Nur 50 Sektoren zum Testen
%define ENABLE_BOOTINFO 1

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov al, '2'
    call print_char

    mov dl, [BOOT_DRIVE_ADDR]
    mov [boot_drive], dl

    ; Kernel-Position
    mov word [load_segment], 0x0800
    mov word [load_offset], 0

    ; Einfache Parameter
    mov byte [sect], 3
    mov byte [head], 0
    mov word [cyl], 0

    ; Begrenzte Sektoren für Test
    mov word [remain], KERNEL_SECTORS_LIMIT

    mov al, 'K'
    call print_char

    ; Einfache Loading-Schleife (single sector)
.load_loop:
    cmp word [remain], 0
    je .load_done

    ; Lade einen Sektor
    mov ax, [load_segment]
    mov es, ax
    mov bx, [load_offset]

    mov ah, 0x02
    mov al, 1        ; Ein Sektor
    mov ch, [cyl]
    mov cl, [sect]
    mov dh, [head]
    mov dl, [boot_drive]
    int 0x13
    jc .load_error

    ; Update
    dec word [remain]
    add word [load_offset], 512
    jnc .advance_pos
    add word [load_segment], 0x1000
    mov word [load_offset], 0

.advance_pos:
    inc byte [sect]
    cmp byte [sect], 18
    jbe .load_loop
    mov byte [sect], 1
    inc byte [head]
    cmp byte [head], 2
    jb .load_loop
    mov byte [head], 0
    inc word [cyl]
    jmp .load_loop

.load_error:
    mov al, 'E'
    call print_char
    jmp .load_done

.load_done:
    mov al, 'P'
    call print_char

    ; A20 aktivieren
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Protected Mode
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
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

    ; VGA Success Indicator
    mov ebx, 0xB8000
    mov word [ebx+0], 0x2F53    ; 'S' gelb auf grün
    mov word [ebx+2], 0x2F54    ; 'T'
    mov word [ebx+4], 0x2F41    ; 'A'
    mov word [ebx+6], 0x2F52    ; 'R'
    mov word [ebx+8], 0x2F54    ; 'T'

    ; Sprung zum Kernel
    jmp 0x8000

; Variablen
boot_drive:   db 0
load_offset:  dw 0
load_segment: dw 0
sect:         db 0
head:         db 0
cyl:          dw 0
remain:       dw 0

; GDT
align 4
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
