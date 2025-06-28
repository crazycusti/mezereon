; bootloader.asm - Minimaler x86-Bootloader, der die nachfolgenden Sektoren lädt und ausführt
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
    mov dh, 2        ; Anzahl Sektoren (kernel ist aktuell 932 bytes groß, daher 2 sektoren..)

load_kernel:
    mov ah, 0x02       ; BIOS: Sektor lesen
    mov al, dh         ; Anzahl Sektoren
    mov ch, 0x00       ; Cylinder
    mov cl, 0x02       ; Sektor (ab 2)
    mov dh, 0x00       ; Head
    mov dl, 0x80       ; Erstes Festplattenlaufwerk
    mov es, ax         ; ES:BX = Zieladresse
    int 0x13           ; BIOS Disk Service
    jc disk_error      ; Fehlerbehandlung

    ; Kernel starten (Sprung zu 0x0000:0x7E00)
    jmp 0x0000:0x7E00

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

times 510-($-$$) db 0
    dw 0xAA55
