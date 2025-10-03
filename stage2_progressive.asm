; stage2_progressive.asm - Mit Progress-Feedback
BITS 16
ORG 0x10000

%include "boot_shared.inc"

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

    mov al, 'A'    ; Setup OK
    call print_char

    ; Versuche nur EINEN Kernel-Sektor zu laden
    mov ax, 0x0800
    mov es, ax
    xor bx, bx

    mov al, 'B'    ; Ready to read
    call print_char

    mov ah, 0x02   ; Read sector
    mov al, 1      ; Ein Sektor
    mov ch, 0      ; Cylinder 0
    mov cl, 3      ; Sektor 3 (nach Stage 1+2)
    mov dh, 0      ; Head 0
    mov dl, [boot_drive]
    int 0x13
    jc .error

    mov al, 'C'    ; Read erfolgsreich
    call print_char

    ; A20 aktivieren
    in al, 0x92
    or al, 2
    out 0x92, al

    mov al, 'D'    ; A20 OK
    call print_char

    ; Protected Mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

.error:
    mov al, 'X'    ; Error
    call print_char
    jmp $

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

    ; VGA Success
    mov ebx, 0xB8000
    mov word [ebx+0], 0x4F4F    ; 'O' weiß auf rot
    mov word [ebx+2], 0x4F4B    ; 'K'

    ; Sprung zum Kernel (auch wenn nur ein Sektor geladen)
    jmp 0x8000

boot_drive: db 0

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
