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

    ; (no banner to save space)

    ; Kernel ab Sektor 2 laden (Sektor 1 ist Bootloader) — robust in Chunks
    ; Ziel: ES:BX = 0x07E0:0x0000 (linear 0x0000:0x7E00)
    mov bx, 0x0000
    mov ax, 0x07E0
    mov es, ax

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
    xor word [cyl], 0
    xor byte [head], 0
    mov byte [sect], 2     ; ab Sektor 2

    ; Verbleibende Sektoren laden
    mov word [remain], KERNEL_SECTORS

.load_loop:
    mov ax, [remain]
    cmp ax, 0
    je .load_done
    ; Sektoren bis Trackende
    xor bx, bx
    mov bl, [spt]
    xor cx, cx
    mov cl, [sect]
    mov si, bx
    sub si, cx             ; si = spt - sect
    inc si                 ; +1 inkl. current
    ; Max pro Call: min(remain, track_left, 127)
    mov di, si             ; di = track_left
    cmp ax, di
    jbe .use_remain
    mov ax, di
.use_remain:
    cmp ax, 127
    jbe .have_n
    mov ax, 127
.have_n:
    ; AX = n_sectors
    push ax
    ; CHS in Register laden
    ; CH = cyl[7:0], CL = (cyl[9:8]<<6)|sect
    mov bx, [cyl]
    mov ch, bl
    mov cl, byte [sect]
    and cl, 0x3F
    mov bl, bh
    and bl, 0x03
    shl bl, 6
    or  cl, bl
    mov dh, [head]
    ; AL = n, AH=0x02, ES:BX = Dest
    pop ax                 ; AX = n
    mov ah, 0x02           ; AH=function 02h, AL=sectors to read
    xor bx, bx             ; BX=0 (ES holds destination segment)
    int 0x13
    jc disk_error
    ; Nach Erfolg: Pointer und CHS fortsetzen
    mov [last_n], al
    ; ES += n * (512/16) = n * 32
    xor ax, ax
    mov al, 32
    mul byte [last_n]      ; AX = last_n * 32
    mov bx, es
    add bx, ax
    mov es, bx
    ; Update remain
    ; Update remain with AL (sectors actually read)
    xor bx, bx
    mov bl, al
    mov ax, [remain]
    sub ax, bx
    mov [remain], ax
    ; Update sector/head/cyl: sect += n
    xor si, si
    mov si, 0
    mov si, 0
    mov bl, [sect]
    mov si, bx
    add si, bx
    ; while sect > spt: sect -= spt; head++
.sect_wrap:
    xor di, di
    mov dl, [spt]
    mov di, dx
    cmp si, di
    jbe .set_sect
    sub si, di
    ; head++
    mov al, [head]
    inc al
    cmp al, [max_head]
    jbe .set_head
    xor al, al
    ; cyl++
    inc word [cyl]
.set_head:
    mov [head], al
    jmp .sect_wrap
.set_sect:
    mov ax, si
    mov [sect], al
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
spt       db 0      ; sectors per track
max_head  db 0      ; maximum head number
sect      db 0      ; current sector (1-based)
head      db 0      ; current head
cyl       dw 0      ; current cylinder
remain    dw 0      ; sectors left to read
last_n    db 0      ; sectors read in last op

times 510-($-$$) db 0
    dw 0xAA55
