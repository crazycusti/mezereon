; stage2_debug.asm - Minimaler Stage 2 für PM Debug
BITS 16
ORG 0x7E00

%include "boot_shared.inc"

; Defaults falls nicht von Makefile gesetzt
%ifndef DEBUG_BOOT
%define DEBUG_BOOT 1
%endif

stage2_start:
%if DEBUG_BOOT
    mov al, '2'       ; Stage 2 gestartet
    call print_char
%endif

    xor ax, ax
    mov ds, ax

    ; Springe sofort in Protected Mode ohne Kernel zu laden
%if DEBUG_BOOT
    mov al, 'P'       ; Preparing Protected Mode
    call print_char
%endif

    ; Protected Mode vorbereiten
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

print_char:
    push ax
    mov ah, 0x0E     ; BIOS TTY Ausgabe
    mov bh, 0        ; Seite 0
    int 0x10
    pop ax
    ret

BITS 32
protected_mode:
    mov ax, 0x10     ; Data Segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00 ; Stack setzen
    
    ; VGA Debug Output
    mov ebx, 0xB8000
    mov word [ebx], 0x0750     ; 'P' - Protected Mode erreicht
    mov word [ebx+2], 0x074D   ; 'M'
    mov word [ebx+4], 0x074F   ; 'O' - OK
    mov word [ebx+6], 0x074B   ; 'K'

.halt:
    hlt
    jmp .halt

; GDT
align 8
gdt_start:
    dq 0x0000000000000000   ; Null Descriptor
    dq 0x00CF9A000000FFFF   ; Code Descriptor (32-bit, 4GB)
    dq 0x00CF92000000FFFF   ; Data Descriptor (32-bit, 4GB)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
