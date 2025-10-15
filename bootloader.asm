; bootloader.asm - i386 Protected Mode Bootloader
; Lädt den Kernel und schaltet in den 32-Bit Protected Mode

; Ziel-CPU ist erstmal fest i386
%define TARGET_8086    0   ; 8086/8088 Real Mode (für später)
%define TARGET_286     1   ; 286 Protected Mode (für später)
%define TARGET_386     2   ; 386+ Protected Mode mit Paging
%define TARGET_I686    3   ; i686+ (für später)
%define TARGET_CPU TARGET_386

%define ENABLE_BOOTINFO 1
%define DEBUG_BOOT 0
%define ENABLE_A20_KBC 0
%define WAIT_BEFORE_PM 0
%define DEBUG_PM_STUB 0

%include "boot_shared.inc"

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

    ; Boot-Drive speichern
    mov [boot_drive], dl

    ; Boot-Info einrichten
    call setup_bootinfo

    ; Lade-Ziel: 0x7E00 (direkt nach Bootloader)
    mov ax, 0x07E0
    mov es, ax          ; ES:BX = 0x07E0:0x0000 = 0x7E00
    xor bx, bx
    
    ; Anzahl zu ladender Sektoren
    mov ax, KERNEL_SECTORS
    mov [remain], ax

    ; Disk zurücksetzen und rekalibrieren
    mov ah, 0x00          ; Reset Disk System
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; Bei Floppy zusätzliche Rekalibrierung
    cmp dl, 0x80
    jae .get_params       ; Bei HDD direkt Parameter holen
    
    mov ah, 0x02          ; Sektor lesen zum Rekalibrieren
    mov al, 1             ; Ein Sektor
    mov ch, 0             ; Cylinder 0
    mov cl, 1             ; Sektor 1
    mov dh, 0             ; Head 0
    mov dl, [boot_drive]
    push es               ; ES:BX sichern
    push bx
    push ax               ; Temporärer Buffer
    mov ax, ss
    mov es, ax
    mov bx, sp
    int 0x13              ; Ignoriere Fehler hier
    pop ax
    pop bx
    pop es

.get_params:
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    jc .use_defaults
    
    mov [max_head], dh
    mov [spt], cl
    and byte [spt], 0x3F
    jmp .init_params

.use_defaults:
    ; Standard Floppy Parameter
    mov byte [spt], 18
    mov byte [max_head], 1

.init_params:
    ; Parameter validieren
    mov al, [max_head]
    or al, al
    jnz .check_spt
    mov byte [max_head], 1  ; Mindestens 2 Köpfe (0-1)
.check_spt:
    mov al, [spt]
    or al, al
    jnz .init_pos
    mov byte [spt], 18      ; Mindestens 18 Sektoren/Spur

.init_pos:
    mov ah, 0
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    test dl, 0x80
    jnz .no_spin
    mov cx, 0x1000
.spin:
    loop .spin
.no_spin:
    xor ax, ax
    mov [cyl], ax
    mov [head], al
    mov byte [sect], 2

.load_loop:
    cmp word [remain], 0    ; Alle Sektoren geladen?
    je .load_done

    ; CHS Parameter vorbereiten
    mov ch, [cyl]          ; CH = Low 8 bits von Cylinder
    mov cl, [sect]         ; CL[5:0] = Sektor
    mov dh, [head]         ; DH = Head
    mov dl, [boot_drive]   ; DL = Drive
    
    mov bl, [cyl+1]        ; Obere Cylinder-Bits
    and bl, 0x03           ; Nur Bits 8-9
    shl bl, 6              ; Nach Position 6-7
    or cl, bl              ; In CL[7:6]

        ; Sektor lesen (3 Versuche)
    mov cx, 3
.retry:
    mov ah, 0x02
    mov al, 1
    mov bx, 0
    push cx
    int 0x13
    pop cx
    jnc .ok
    
    push ax
    mov ah, 0
    int 0x13
    mov cx, 0x800
.wait:
    loop .wait
    pop ax
    loop .retry
    jmp disk_error
.ok:

.read_ok:
    ; Buffer für nächsten Sektor vorbereiten
    mov ax, es
    add ax, 0x20         ; 512 Bytes weiter
    mov es, ax
    dec word [remain]

    mov al, [sect]
    inc al
    cmp al, [spt]
    jbe .chs_store_sect
    mov al, 1
    mov bl, [head]
    inc bl
    cmp bl, [max_head]
    jbe .chs_store_head
    mov bl, 0
    inc word [cyl]
.chs_store_head:
    mov [head], bl
.chs_store_sect:
    mov [sect], al
    jmp .load_loop

.load_done:
    ; A20 Gate aktivieren
    call enable_a20
%if DEBUG_BOOT
    mov al, 'P'
    call print_char
%endif

    ; GDT vorbereiten (im Bootsektor, 3 Einträge: Null, Code, Data)
%if DEBUG_BOOT
    mov al, 'T'
    call print_char
%endif

%if WAIT_BEFORE_PM
%if DEBUG_BOOT
    mov al, 'W'
    call print_char
%endif
.wait_pm_loop:
    hlt
    jmp .wait_pm_loop
%endif
    cli
    lgdt [gdt_descriptor]
    ; (no print before protected mode)

    ; Protected Mode aktivieren
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Weitsprung in 32-Bit Protected Mode (CS: 0x08)
jmp 0x08:protected_mode_entry

%if DEBUG_BOOT
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

print_dec_byte:
    push ax
    push bx
    xor ah, ah
    mov bl, 10
    div bl
    cmp al, 0
    je .skip_high
    add al, '0'
    call print_char
.skip_high:
    mov al, ah
    add al, '0'
    call print_char
    pop bx
    pop ax
    ret
%endif

%if ENABLE_BOOTINFO
; --- Boot info helpers ---
setup_bootinfo:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ax, BOOTINFO_SEG
    mov es, ax
    xor di, di
    xor ax, ax
    mov cx, BOOTINFO_CLEAR_WORDS
    cld
    rep stosw

    xor ax, ax
    mov ds, ax
    mov es, ax

    xor eax, eax
    mov dword [BOOTINFO_MACHINE_OFF], eax
    mov dword [BOOTINFO_CONSOLE_OFF], eax
    mov dword [BOOTINFO_FLAGS_OFF], eax
    mov dword [BOOTINFO_PROM_OFF], eax
    mov dword [BOOTINFO_BOOTDEV_OFF], eax
    mov dword [BOOTINFO_MEM_COUNT_OFF], eax

    mov eax, BI_ARCH_X86
    mov dword [BOOTINFO_ARCH_OFF], eax

    movzx eax, byte [boot_drive]
    mov dword [BOOTINFO_BOOTDEV_OFF], eax

    cmp byte [boot_drive], 0x80
    jb .no_hdd_flag
    mov eax, BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD
    mov dword [BOOTINFO_FLAGS_OFF], eax
.no_hdd_flag:

    call detect_memory
    ; --- Probe current BIOS video mode (INT 10h AH=0x0F)
    push ax
    push bx
    push cx
    push dx
    mov ah, 0x0F
    int 0x10
    ; AL = current mode
    movzx eax, al
    mov word [BOOTINFO_VBE_MODE], ax

    ; Allocate scratch buffer at BOOTINFO_ADDR + 0x20 (ES:DI) and call VBE 0x4F00
    mov ax, BOOTINFO_SEG
    mov es, ax
    mov di, BOOTINFO_BIAS
    add di, 0x20            ; scratch buffer offset
    ; clear first dword
    mov word [es:di], 0
    mov word [es:di+2], 0

    mov ax, 0x4F00
    mov bx, 0
    int 0x10
    jc .vbe_none
    cmp ax, 0x004F
    jne .vbe_none

    ; VBE controller info present at ES:DI (we used buffer at BOOTINFO_ADDR+0x20)
    ; VideoModePtr is a far pointer at offset 0x0E (word offset, word segment)
    ; Read mode list pointer into SI:DX (SI = offset, DX = segment)
    mov si, [es:di+0x0E]
    mov dx, [es:di+0x10]


    ; Iterate mode list: words terminated by 0xFFFF
.mode_loop:
    ; read next mode word from DX:SI
    push es
    push ds
    mov ax, DX
    mov ds, ax
    mov bx, SI
    mov ax, [ds:bx]
    ; check terminator
    cmp ax, 0xFFFF
    je .mode_done


    ; Query ModeInfo for this mode: call INT 0x10 AX=0x4F01 CX=mode with ES:DI -> scratch
    mov ax, 0x4F01
    mov cx, ax              ; save mode in cx
    ; set ES:DI to BOOTINFO scratch again
    mov ax, BOOTINFO_SEG
    mov es, ax
    mov di, BOOTINFO_BIAS
    add di, 0x20
    int 0x10
    jc .skip_mode
    cmp ax, 0x004F
    jne .skip_mode

    ; Parse ModeInfo in scratch buffer: offsets (per VBE spec-ish)
    ; bytes_per_scanline: word at +0x10
    ; xres: word at +0x12, yres: word at +0x14
    ; bpp: byte at +0x19
    ; physbaseptr: dword at +0x28
    mov ax, [es:di+0x10]
    mov [BOOTINFO_VBE_PITCH], ax
    mov ax, [es:di+0x12]
    mov [BOOTINFO_VBE_WIDTH], ax
    mov ax, [es:di+0x14]
    mov [BOOTINFO_VBE_HEIGHT], ax
    mov al, [es:di+0x19]
    mov [BOOTINFO_VBE_BPP], al
    ; phys base ptr (dword)
    mov bx, [es:di+0x28]
    mov cx, [es:di+0x2A]
    mov [BOOTINFO_FB_ADDR], bx
    mov [BOOTINFO_FB_ADDR+2], cx

    ; If we found a linear framebuffer address, keep BOOTINFO updated (no Stage2 logging)
    mov ax, [BOOTINFO_FB_ADDR]
    or ax, [BOOTINFO_FB_ADDR+2]
    jz .no_fb_log
    ; (no logging here - kernel will display the results)
.no_fb_log:

.skip_mode:
    pop ds
    pop es
    ; advance SI to next word
    add si, 2
    jmp .mode_loop
.mode_done:
    jmp .vbe_done
.vbe_none:
    ; VBE not present - silent
.vbe_done:
    pop dx
    pop cx
    pop bx
    pop ax
    ; continue setup
    ; NOTE: if we didn't find an FB, BOOTINFO_FB_ADDR remains zero

detect_memory:
    xor ebx, ebx
    mov di, BOOTINFO_MEM_ENTRIES_BIAS
    xor si, si

.detect_loop:
    mov ax, BOOTINFO_SEG
    mov es, ax
    mov eax, 0xE820
    mov edx, SMAP_SIGNATURE
    mov ecx, BOOTINFO_MEM_ENTRY_SIZE
    int 0x15
    jc .done
    cmp eax, SMAP_SIGNATURE
    jne .done
    cmp si, BOOTINFO_MEM_MAX
    jae .done
    inc si
    add di, BOOTINFO_MEM_ENTRY_SIZE
.skip:
    cmp ebx, 0
    jne .detect_loop
.done:
    mov word [BOOTINFO_MEM_COUNT_OFF], si
    ret
%endif

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

%if DEBUG_PM_STUB
    mov ebx, 0xB8000
    mov word [ebx], 0x0744         ; 'D'
    mov word [ebx+2], 0x0742       ; 'B'
    mov word [ebx+4], 0x0747       ; 'G'
    mov word [ebx+6], 0x0758       ; 'X'
.pm_debug_loop:
    hlt
    jmp .pm_debug_loop
%else
    ; Sprung zum Kernel-Einsprungspunkt (0x7E00, 32-bit Entry in payload)
    jmp 0x7E00
%endif

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
%if DEBUG_BOOT
    mov al, 'E'
    call print_char
%endif
    jmp $

; A20 Gate aktivieren
enable_a20:
    push ax
    ; Schneller Versuch über Port 0x92 (PS/2)
    in al, 0x92
    or al, 2
    out 0x92, al
%if ENABLE_A20_KBC
    in al, 0x92
    test al, 2
    jnz .done
    call enable_a20_kbc
%endif
.done:
    pop ax
    ret

%if ENABLE_A20_KBC
enable_a20_kbc:
    push ax
    call enable_a20_wait_ibf_clear
    mov al, 0xAD
    out 0x64, al
    call enable_a20_wait_ibf_clear
    mov al, 0xD0
    out 0x64, al
    call enable_a20_wait_obf_set
    in al, 0x60
    or al, 2
    mov ah, al
    call enable_a20_wait_ibf_clear
    mov al, 0xD1
    out 0x64, al
    call enable_a20_wait_ibf_clear
    mov al, ah
    out 0x60, al
    call enable_a20_wait_ibf_clear
    mov al, 0xAE
    out 0x64, al
    call enable_a20_wait_ibf_clear
    pop ax
    ret

enable_a20_wait_ibf_clear:
    in al, 0x64
    test al, 2
    jnz enable_a20_wait_ibf_clear
    ret

enable_a20_wait_obf_set:
    in al, 0x64
    test al, 1
    jz enable_a20_wait_obf_set
    ret
%endif

cpu_error:
    jmp $               ; Endlosschleife

; --- Daten (müssen vor Signatur liegen) ---
; Disk Parameter Block
disk_info:
boot_drive db 0     ; BIOS drive number (from DL)
remain     dw 0     ; sectors left to read
retry_count db 3    ; maximale Leseversuche
disk_error_code db 0 ; letzter Fehlercode
spt        db 0     ; Sektoren pro Spur
max_head   db 0     ; Maximale Head-Nummer (Köpfe - 1)
sect       db 0     ; Aktueller Sektor
head       db 0     ; Aktueller Head
cyl        dw 0     ; Aktueller Cylinder

; Temporäre Speicherung für ES:BX
save_es    dw 0     ; Segment sichern
save_bx    dw 0     ; Offset sichern

times 510-($-$$) db 0
    dw 0xAA55
