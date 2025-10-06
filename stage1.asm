; stage1_fixed.asm - Stage 1 mit korrigierter Ladeadresse
BITS 16
ORG 0x7C00

%include "boot_config.inc"

%define DEBUG_BOOT ENABLE_STAGE1_DEBUG
; reserve offset 0x1F in the boot sector for the boot drive byte
times 31-($-$$) db 0
boot_drive: db 0

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
    mov al, '.'      ; Stack OK
    call print_char
%endif

    ; Boot-Drive speichern
    mov [boot_drive], dl
%if DEBUG_BOOT
    mov al, 'D'      ; Drive saved
    call print_char
%endif

    ; Diskette zurücksetzen
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    jc error

    ; Stage 2 laden - NEUE ADRESSE: 0x1000 statt 0x7E00
    ; Das vermeidet DMA-Boundary-Probleme
    mov ax, STAGE2_LOAD_SEGMENT   ; Stage 2 load segment (0x10000 phys by default)
    mov es, ax
    xor bx, bx       ; ES:BX = 0x1000:0000

%if DEBUG_BOOT
    mov al, 'L'      ; Loading Stage 2
    call print_char
%endif

    mov ah, 0x02     ; Read Sectors
    mov al, STAGE2_SECTORS        ; How many sectors to fetch
    mov ch, 0        ; Cylinder 0
    mov cl, STAGE2_START_SECTOR   ; First sector after boot sector
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
    mov al, 'O'      ; Load OK
    call print_char
%endif

%if ENABLE_STAGE1_A20
    call enable_a20_fast
%endif

    ; Zu Stage 2 springen - NEUE ADRESSE
    jmp STAGE2_LOAD_SEGMENT:0000

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

%if ENABLE_STAGE1_A20
enable_a20_fast:
    in al, 0x92
    test al, 2
    jnz .done
    or al, 2
    out 0x92, al
.done:
    ret
%endif

; Boot-Signatur
times 510-($-$$) db 0
dw 0xAA55
