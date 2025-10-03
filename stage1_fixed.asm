; stage1_fixed.asm - Stage 1 mit korrigierter Ladeadresse
BITS 16
ORG 0x7C00

%include "boot_shared.inc"

%define DEBUG_BOOT 1

start:
    ; Stack einrichten
    cli
    mov ax, 0
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax
    sti

%if DEBUG_BOOT
    mov al, '1'      ; Stage 1 gestartet
    call print_char
%endif

    ; Boot-Drive speichern
    mov [boot_drive], dl

    ; Diskette zurücksetzen
    xor ax, ax
    mov dl, [boot_drive] 
    int 0x13
    jc error

    ; Stage 2 laden - NEUE ADRESSE: 0x1000 statt 0x7E00
    ; Das vermeidet DMA-Boundary-Probleme
    mov ax, 0x1000   ; Stage 2 bei 0x10000 (64KB)
    mov es, ax
    xor bx, bx       ; ES:BX = 0x1000:0000

%if DEBUG_BOOT
    mov al, 'L'      ; Loading Stage 2
    call print_char
%endif

    mov ah, 0x02     ; Read Sectors
    mov al, 1        ; Ein Sektor
    mov ch, 0        ; Cylinder 0
    mov cl, 2        ; Sektor 2 (nach Stage 1)
    mov dh, 0        ; Head 0
    mov dl, [boot_drive]
    int 0x13
    jc error

%if DEBUG_BOOT
    mov al, 'O'      ; Load OK
    call print_char
%endif

    ; Zu Stage 2 springen - NEUE ADRESSE
    jmp 0x1000:0000

error:
%if DEBUG_BOOT
    mov al, 'E'      ; Error
    call print_char
    ; Error Code ausgeben (vereinfacht)
    mov al, ah
    and al, 0x0F     ; Nur unteres Nibble
    add al, '0'
    call print_char
%endif
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

times BOOT_DRIVE_OFFSET - ($-$$) db 0
boot_drive: db 0

times 510-($-$$) db 0
dw 0xAA55
