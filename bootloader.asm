; bootloader.asm - Minimaler 8086-Bootloader, der die nachfolgenden Sektoren lädt und ausführt
; Annahme: Kernel (aus main.c kompiliert) liegt direkt hinter dem Bootsektor

BITS 16
ORG 0x7C00

start:
    ; Stack einrichten
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Kernel ab Sektor 2 laden (Sektor 1 ist Bootloader)
    mov si, 0          ; Offset für Kernel im Speicher (0x0000:0x7E00)
    mov bx, 0x7E00     ; Zieladresse im RAM
    mov dh, 10        ; Anzahl Sektoren (kernel ist aktuell 932 bytes groß, daher 2 sektoren..)

load_kernel:
    mov ah, 0x02       ; BIOS: Sektor lesen
    mov al, dh         ; Anzahl Sektoren
    mov ch, 0x00       ; Cylinder
    mov cl, 0x02       ; Sektor (ab 2)
    mov dh, 0x00       ; Head
    mov dl, 0x80       ; Erstes Festplattenlaufwerk
    xor ax, ax
    mov es, ax
    int 0x13           ; BIOS Disk Service
    jc disk_error      ; Fehlerbehandlung

    mov si, success_msg
    call print_string


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

    ; Sprung zum Kernel-Einsprungspunkt (0x7E00)
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

times 510-($-$$) db 0
    dw 0xAA55
