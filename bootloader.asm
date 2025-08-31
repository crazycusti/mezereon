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

    ; BIOS Bootlaufwerk merken (DL) und erkennen (HDD/FDD)
    mov [boot_drive], dl
    call detect_boot_device
    cmp byte [boot_device_type], 0
    jne .boot_hdd
    mov si, boot_fdd_msg
    call print_string
    jmp .after_bootmsg
.boot_hdd:
    mov si, boot_hdd_msg
    call print_string
.after_bootmsg:

    ; Begrüßung anzeigen
    mov si, boot_msg
    call print_string

    ; Kernel ab Sektor 2 laden (Sektor 1 ist Bootloader)
    mov bx, 0x7E00     ; Zieladresse im RAM (ES:BX)
    xor ax, ax
    mov es, ax         ; ES = 0x0000

load_kernel:
    ; Einheitlicher CHS-Pfad für HDD/FDD: trackweise lesen (robust)
    ; Geometrie abfragen (AH=08)
    mov dl, [boot_drive]
    mov ah, 0x08
    int 0x13
    jc .geom_fallback
    mov al, cl
    and al, 0x3F
    mov [chs_spt], al
    mov [chs_heads], dh
    jmp .geom_ok
.geom_fallback:
    mov byte [chs_spt], 18
    mov byte [chs_heads], 1
.geom_ok:
    ; Reset
    mov dl, [boot_drive]
    xor ah, ah
    int 0x13
    ; Startposition
    xor ch, ch
    xor dh, dh
    mov cl, 2
    mov al, KERNEL_SECTORS
    mov [remain_sectors], al

.read_loop:
    mov al, [remain_sectors]
    or al, al
    jz .read_done
    ; rem_track = spt - (cl-1)
    mov ah, [chs_spt]
    mov bl, cl
    dec bl
    sub ah, bl
    cmp al, ah
    jbe .count_ok
    mov al, ah
.count_ok:
    mov [last_count], al
    ; kleiner Fortschrittsindikator (AL sichern!)
    push ax
    mov al, '.'
    mov ah, 0x0E
    int 0x10
    pop ax                 ; AL enthält wieder last_count
    mov ah, 0x02           ; BIOS Read: AH=0x02, AL=count
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    
    ; Advance buffer by count*512
    mov ah, 0
    push cx
    mov cl, 9
    shl ax, cl
    pop cx
    add bx, ax
    jnc .no_carry
    push es
    pop dx
    inc dx
    push dx
    pop es
.no_carry:
    ; remain -= count
    mov al, [remain_sectors]
    sub al, [last_count]
    mov [remain_sectors], al
    ; CL += count, Trackwechsel
    mov al, [last_count]
    add cl, al
.track_fix:
    cmp cl, [chs_spt]
    jbe .cont
    sub cl, [chs_spt]
    inc dh
    cmp dh, [chs_heads]
    jbe .cont
    mov dh, 0
    inc ch
    jmp .track_fix
.cont:
    jmp .read_loop

.read_done:

    mov si, success_msg
    call print_string

    ; CPU-Check übersprungen (Platz sparen)

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
    mov eax, 0x7E00     ; absoluter Sprung (EIP = 0x7E00)
    jmp eax

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
    cld
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
boot_hdd_msg db 'Boot drive: HDD/EDD', 13, 10, 0
boot_fdd_msg db 'Boot drive: FDD', 13, 10, 0
boot_drive db 0
boot_device_type db 0
chs_spt db 18
chs_heads db 1
remain_sectors db 0
last_count db 0

; (CPU-Check entfernt um Platz zu sparen)
; removed debug strings to save space

; A20 Gate aktivieren
enable_a20:
    ; Schneller Versuch über Port 0x92 (PS/2)
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

; cpu_error handler nicht mehr verwendet

; ---------------------------------
; Bootlaufwerk erkennen (HDD/FDD)
; Setzt boot_device_type: 0=FDD, 1=HDD/EDD
; Nutzt EDD 0x41 wenn verfügbar, sonst DL Bit7
detect_boot_device:
    mov dl, [boot_drive]
    test dl, 0x80
    jz .is_fdd
    mov byte [boot_device_type], 1
    ret
.is_fdd:
    mov byte [boot_device_type], 0
    ret

times 510-($-$$) db 0
    dw 0xAA55
