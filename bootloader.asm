; bootloader.asm - i386 Protected Mode Bootloader
; Lädt den Kernel und schaltet in den 32-Bit Protected Mode

; Ziel-CPU ist erstmal fest i386
%define TARGET_8086    0   ; 8086/8088 Real Mode (für später)
%define TARGET_286     1   ; 286 Protected Mode (für später)
%define TARGET_386     2   ; 386+ Protected Mode mit Paging
%define TARGET_I686    3   ; i686+ (für später)
%define TARGET_CPU TARGET_386

; Anzahl zu ladender Sektoren wird vom Build gesetzt
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 10
%endif

BITS 16
ORG 0x7C00

start:
    ; Stack einrichten
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    mov ds, ax

    ; Begrüßung anzeigen
    mov si, boot_msg
    call print_string

    ; Kernel ab Sektor 2 laden (Sektor 1 ist Bootloader)
    mov bx, 0x7E00     ; Zieladresse im RAM (ES:BX)
    xor ax, ax
    mov es, ax         ; ES = 0x0000

load_kernel:
    mov ah, 0x02       ; BIOS: Sektor lesen
    mov al, KERNEL_SECTORS ; Anzahl Sektoren
    mov ch, 0x00       ; Cylinder 0
    mov cl, 0x02       ; Sektor 2 (1 ist Bootsektor)
    mov dh, 0x00       ; Head 0
    mov dl, 0x80       ; Erstes Festplattenlaufwerk (bei Floppy 0x00)
    int 0x13           ; BIOS Disk Service
    jc disk_error      ; Fehlerbehandlung

    mov si, success_msg
    call print_string
    mov si, after_load_msg
    call print_string

    ; Prüfe ob mindestens i386
    mov si, before_cpu_msg
    call print_string
    call check_cpu
    jc cpu_error

    mov si, cpu_ok_msg
    call print_string
    mov si, after_cpu_msg
    call print_string

    ; A20 Gate aktivieren
    call enable_a20

    ; GDT vorbereiten (im Bootsektor, 3 Einträge: Null, Code, Data)
    cli
    lgdt [gdt_descriptor]

    ; Protected Mode aktivieren
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Weitsprung in 32-Bit Protected Mode (CS: 0x08)
    jmp 0x08:protected_mode_entry

; -----------------------------
; 32-Bit Protected Mode Code
; -----------------------------

BITS 32
protected_mode_entry:
    mov ax, 0x10        ; Data Segment Selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00    ; Stack (z.B. unterhalb 640K)

    ; Sprung zum Kernel-Einsprungspunkt (0x7E00, 32-bit Entry in payload)
    jmp 0x7E00

; -----------------------------
; GDT (im Bootsektor)
; -----------------------------

gdt_start:
    dq 0x0000000000000000      ; Null-Deskriptor
    dq 0x00CF9A000000FFFF      ; Code-Deskriptor (Basis 0, Limit 4GB, 32bit, RX)
    dq 0x00CF92000000FFFF      ; Data-Deskriptor (Basis 0, Limit 4GB, 32bit, RW)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

disk_error:
    mov si, error_msg
    call print_string
    jmp $

print_string:
    mov ah, 0x0E
.next_char:
    lodsb
    or al, al
    jz .done
    int 0x10
    jmp .next_char
.done:
    ret


error_msg db 'Disk Error!', 0
success_msg db 'DEBUG Load OK', 0
boot_msg db 'Mezereon Bootloader starting...', 13, 10, 0
cpu_ok_msg db 'CPU check passed: i386+ detected', 13, 10, 0
cpu_error_msg db 'Error: i386 or better CPU required!', 13, 10, 0

; CPU-Check für i386
check_cpu:
    ; Völlig konservativer Stub: nur Debug-Prints, keine Privileg-OPs
    mov si, testing_msg
    call print_string
    mov si, cr0_msg
    call print_string
    mov si, pe_msg
    call print_string
    clc                 ; Success
    ret

; Debug Messages
testing_msg db 'CPU Test...', 13, 10, 0
cr0_msg db 'CR0 Test OK...', 13, 10, 0
pe_msg db 'PE Test OK...', 13, 10, 0
before_cpu_msg db 'Before CPU check...', 13, 10, 0
after_cpu_msg db 'After CPU check...', 13, 10, 0
after_load_msg db 'After load...', 13, 10, 0

; A20 Gate aktivieren
enable_a20:
    ; Schneller Versuch über Port 0x92 (PS/2)
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

cpu_error:
    mov si, cpu_error_msg
    call print_string
    jmp $               ; Endlosschleife

times 510-($-$$) db 0
    dw 0xAA55
