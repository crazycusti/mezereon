; stage1.asm - Erster Stage des Bootloaders
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
    int 0x13
    jc error

    ; Stage 2 laden
    mov ax, 0x07E0   ; Stage 2 bei 0x7E00
    mov es, ax
    xor bx, bx       ; ES:BX = 0x7E00

    mov ah, 0x02     ; Read Sectors
    mov al, 1        ; Ein Sektor
    mov ch, 0        ; Cylinder 0
    mov cl, 2        ; Sektor 2
    mov dh, 0        ; Head 0
    mov dl, [boot_drive]
    mov si, 3        ; 3 Versuche

.retry:
    pusha
    int 0x13
    popa
    jnc .read_ok

    ; Reset und neu versuchen
    dec si
    jz error
    push ax
    xor ax, ax
    int 0x13
    pop ax
    jmp .retry

.read_ok:
%if DEBUG_BOOT
    mov al, 'L'      ; Stage 2 geladen
    call print_char
%endif

    ; Zu Stage 2 springen
    jmp 0x7E00

error:
%if DEBUG_BOOT
    mov al, 'E'      ; Error
    call print_char
%endif
    jmp $

print_char:
    push ax
    mov ah, 0x0E    ; BIOS TTY Ausgabe
    mov bh, 0       ; Seite 0
    int 0x10
    pop ax
    ret

; Variablen
times BOOT_DRIVE_OFFSET - ($-$$) db 0
boot_drive: db 0

; Boot-Signatur
times 510-($-$$) db 0
dw 0xAA55
