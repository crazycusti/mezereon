; stage1_fixed.asm - Stage 1 mit korrigierter Ladeadresse
BITS 16
ORG 0x7C00

%include "boot_config.inc"
%include "boot_shared.inc"

%define DEBUG_BOOT ENABLE_STAGE1_DEBUG

; ---------------------------------------------------------------------------
; BIOS Parameter Block (FAT12-compatible) so legacy BIOSes accept the boot record
; ---------------------------------------------------------------------------
jmp short start
nop
OEMLabel            db 'MEZRN   '
BytesPerSector      dw 512
SectorsPerCluster   db 1
ReservedSectors     dw 1
NumberOfFATs        db 2
RootEntries         dw 224
TotalSectors16      dw 2880
MediaDescriptor     db 0xF0
SectorsPerFAT       dw 9
SectorsPerTrack     dw 18
NumberOfHeads       dw 2
HiddenSectors       dd 0
TotalSectors32      dd 0
DriveNumber         db 0
Reserved1           db 0
BootSignature       db 0x29
VolumeSerial        dd 0x20240315
VolumeLabel         db 'MEZBOOT    '
FileSystemType      db 'FAT12   '

start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax
    sti

%if DEBUG_BOOT
    mov al, '1'
    call print_char
    mov al, '.'
    call print_char
%endif

    mov [boot_drive], dl
%if DEBUG_BOOT
    mov al, 'D'
    call print_char
%endif

    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    jc error

    mov ax, STAGE2_LOAD_SEGMENT
    mov es, ax
    xor bx, bx

%if DEBUG_BOOT
    mov al, 'L'
    call print_char
%endif

    mov ah, 0x02
    mov al, STAGE2_SECTORS
    mov ch, 0
    mov cl, STAGE2_START_SECTOR
    mov dh, 0
    mov dl, [boot_drive]
    mov si, 3

.retry:
    pusha
    int 0x13
    popa
    jnc .read_ok

    dec si
    jz error
    push ax
    xor ax, ax
    int 0x13
    pop ax
    jmp .retry

.read_ok:
%if DEBUG_BOOT
    mov al, 'O'
    call print_char
%endif

    jmp STAGE2_LOAD_SEGMENT:0000

error:
%if DEBUG_BOOT
    mov al, 'E'
    call print_char
    mov al, ah
    and al, 0x0F
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
boot_drive db 0

times 510-($-$$) db 0
dw BOOT_SIGNATURE
