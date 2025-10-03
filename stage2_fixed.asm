; stage2_fixed.asm - Stage 2 mit korrigierter Stack-Behandlung
BITS 16
ORG 0x7E00

%include "boot_shared.inc"

%ifndef DEBUG_BOOT
%define DEBUG_BOOT 1
%endif
%ifndef KERNEL_SECTORS  
%define KERNEL_SECTORS 60
%endif
%ifndef DEBUG_PM_STUB
%define DEBUG_PM_STUB 0
%endif

stage2_start:
    ; **KRITISCH**: Stack und Segmente neu initialisieren!
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00    ; Stack vor Stage 1
    sti

%if DEBUG_BOOT
    mov al, '2'       ; Stage 2 gestartet
    call print_char
%endif

    ; Boot-Drive von Stage 1 lesen
    mov dl, [BOOT_DRIVE_ADDR] 
    mov [boot_drive], dl

    ; **SKIP KERNEL LOADING FÜR JETZT** - gehe direkt zu Protected Mode
%if DEBUG_BOOT
    mov al, 'P'       ; Preparing Protected Mode
    call print_char
%endif

    ; A20 Gate aktivieren (wichtig!)
    ; call enable_a20 ; DISABLE FOR TEST
%endif

    ; Protected Mode aktivieren
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

enable_a20:
    ; Schneller A20 über Port 0x92
    push ax
    in al, 0x92
    or al, 2
    out 0x92, al
    pop ax
    ret

print_char:
    push ax
    push bx
    mov ah, 0x0E     ; BIOS TTY
    mov bh, 0        ; Page 0
    mov bl, 0x07     ; Attribute
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
    mov esp, 0x9FC00 ; Stack in hohem Speicher

%if DEBUG_PM_STUB
    ; VGA Test Output 
    mov ebx, 0xB8000
    mov word [ebx], 0x0750     ; 'P' 
    mov word [ebx+2], 0x074D   ; 'M'
    mov word [ebx+4], 0x074F   ; 'O'
    mov word [ebx+6], 0x074B   ; 'K'
.pm_debug_loop:
    hlt
    jmp .pm_debug_loop
%else
    ; Sprung zum Kernel bei 0x8000
    jmp 0x8000
%endif

; Daten
boot_drive: db 0

; GDT
align 8
gdt_start:
    dq 0x0000000000000000   ; Null Descriptor
    dq 0x00CF9A000000FFFF   ; Code: Base=0, Limit=4GB, 32-bit, Execute+Read
    dq 0x00CF92000000FFFF   ; Data: Base=0, Limit=4GB, 32-bit, Read+Write  
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
