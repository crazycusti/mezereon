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

    mov [boot_drive], dl

    ; Kernel ab Sektor 2 laden (Sektor 1 ist Bootloader) — robust in Chunks
    ; Ziel: ES:BX = 0x07E0:0x0000 (linear 0x0000:0x7E00)
    mov ax, 0x07E0
    mov es, ax
    xor bx, bx

    ; Ermittele Geometrie (Sektoren/Spur, Köpfe)
    push dx                ; Bootlaufwerk in DL sichern
    mov ah, 0x08
    int 0x13
    pop dx
    jc disk_error
    mov byte [spt], cl     ; CL[5:0] = Sektoren/Spur
    and byte [spt], 0x3F
    mov byte [max_head], dh ; DH = max Head (0..n)
    ; Initiale CHS
    xor ax, ax
    mov [cyl], ax
    mov byte [head], 0
    mov byte [sect], 2     ; ab Sektor 2

    ; Verbleibende Sektoren laden
    mov ax, KERNEL_SECTORS
    mov [remain], ax

    ; Versuche BIOS LBA-Erweiterung (EDD)
    mov byte [use_lba], 0
    mov word [lba_lo], 1
    mov word [lba_hi], 0
    mov word [dap+12], 0
    mov word [dap+14], 0
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .lba_check_done
    cmp bx, 0xAA55
    jne .lba_check_done
    test cx, 1
    jz .lba_check_done
    mov byte [use_lba], 1
.lba_check_done:

.load_loop:
    mov ax, [remain]
    cmp ax, 0
    je .load_done

    ; Sektoren bis Trackende (track_left = spt - sect + 1)
    movzx bx, byte [spt]
    movzx cx, byte [sect]
    sub bx, cx
    inc bx
    mov dx, bx                    ; dx = track_left

    ; n = min(remain, track_left, 127)
    mov ax, [remain]
    cmp ax, dx
    jbe .after_track_limit
    mov ax, dx
.after_track_limit:
    cmp ax, 32
    jbe .count_ready
    mov ax, 32
.count_ready:
    mov [n_sectors], ax           ; remember sectors for this call

    cmp byte [use_lba], 0
    jne .use_lba_path

    ; CHS vorbereiten
    mov bx, [cyl]
    mov ch, bl                    ; CH = cyl low byte
    mov cl, byte [sect]
    and cl, 0x3F
    mov dh, [head]
    mov dl, [boot_drive]
    mov bl, bh                    ; cyl high bits -> CL[7:6]
    and bl, 0x03
    shl bl, 6
    or  cl, bl

    mov ah, 0x02                  ; BIOS: Sektor lesen
    mov al, byte [n_sectors]
    xor bx, bx                    ; Zieloffset 0 (ES trägt Segment)
    int 0x13
    jc disk_error
    jmp .after_read

.use_lba_path:
    mov ax, [n_sectors]
    mov [dap+2], ax               ; sectors to read
    mov word [dap+4], 0           ; buffer offset
    mov word [dap+6], es          ; buffer segment
    mov ax, [lba_lo]
    mov [dap+8], ax
    mov ax, [lba_hi]
    mov [dap+10], ax
    mov ah, 0x42
    mov dl, [boot_drive]
    mov si, dap
    int 0x13
    jc disk_error
    xor dx, dx
    mov ax, [n_sectors]
    add [lba_lo], ax
    adc [lba_hi], dx

.after_read:

    ; Zeiger und Rest aktualisieren
    mov ax, [remain]
    sub ax, [n_sectors]
    mov [remain], ax

    mov ax, [n_sectors]
    shl ax, 5                     ; n * (512/16)
    mov bx, es
    add bx, ax
    mov es, bx

    ; Nächste CHS Position berechnen
    cmp byte [use_lba], 0
    jne .skip_chs_update
    movzx ax, byte [sect]
    add ax, [n_sectors]

.wrap_check:
    movzx bx, byte [spt]
    cmp ax, bx
    jbe .store_sect
    sub ax, bx
    mov bl, [head]
    inc bl
    cmp bl, [max_head]
    jbe .store_head
    mov bl, 0
    inc word [cyl]
.store_head:
    mov [head], bl
    jmp .wrap_check

.store_sect:
    mov [sect], al
    jmp .load_loop

.skip_chs_update:
    jmp .load_loop

.load_done:

    ; load complete

    ; Prüfe ob mindestens i386 (vorerst übersprungen)
    ; skip cpu check / prints to save space

    ; A20 Gate aktivieren
    call enable_a20

    ; GDT vorbereiten (im Bootsektor, 3 Einträge: Null, Code, Data)
    cli
    lgdt [gdt_descriptor]
    ; (no print before protected mode)

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
    mov [disk_err_status], ah
    jmp $

; A20 Gate aktivieren
enable_a20:
    ; Schneller Versuch über Port 0x92 (PS/2)
    in al, 0x92
    or al, 2
    out 0x92, al
    ret

cpu_error:
    jmp $               ; Endlosschleife

; --- Daten (müssen vor Signatur liegen) ---
boot_drive db 0     ; BIOS drive number (from DL)
spt        db 0     ; sectors per track
max_head   db 0     ; maximum head number
sect       db 0     ; current sector (1-based)
head       db 0     ; current head
cyl        dw 0     ; current cylinder
remain     dw 0     ; sectors left to read
n_sectors  dw 0     ; sectors read in last op
disk_err_status db 0 ; last BIOS error code
use_lba    db 0
lba_lo     dw 1
lba_hi     dw 0
dap:        db 0x10, 0x00
            dw 0      ; sector count
            dw 0      ; buffer offset
            dw 0      ; buffer segment
            dw 0, 0   ; LBA low dword (initial zero; set at runtime)
            dw 0, 0   ; LBA high dword (unused)

times 510-($-$$) db 0
    dw 0xAA55
