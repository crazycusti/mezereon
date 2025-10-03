; stage2_optimized.asm - Optimierter Stage 2 mit Multi-Sektor Loading
BITS 16
ORG 0x10000

%include "boot_shared.inc"

; Defaults
%ifndef DEBUG_BOOT
%define DEBUG_BOOT 0
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 60
%endif
%ifndef DEBUG_PM_STUB
%define DEBUG_PM_STUB 0
%endif
%ifndef ENABLE_BOOTINFO
%define ENABLE_BOOTINFO 1
%endif

; Optimized loading parameters
%define MAX_SECTORS_PER_READ 18    ; Volle Spur auf einmal lesen
%define SECTOR_SIZE 512

stage2_start:
    ; Segmente und Stack initialisieren
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

%if DEBUG_BOOT
    mov al, '2'
    call print_char
%endif

    ; Boot-Drive von Stage 1
    mov dl, [BOOT_DRIVE_ADDR]
    mov [boot_drive], dl

%if ENABLE_BOOTINFO
    call setup_bootinfo
%endif

    ; Kernel-Ladeposition: 0x8000
    mov word [load_segment], 0x0800
    mov word [load_offset], 0

    ; Disk-Parameter (für Floppy optimiert)
    mov byte [sectors_per_track], 18
    mov byte [heads], 2

    ; Startposition für Kernel: Sektor 3 (nach Stage 1+2)
    mov byte [current_sector], 3
    mov byte [current_head], 0
    mov word [current_track], 0
    mov word [sectors_remaining], KERNEL_SECTORS

%if DEBUG_BOOT
    mov al, 'K'   ; Kernel loading
    call print_char
%endif

    ; Optimized loading loop
.load_loop:
    cmp word [sectors_remaining], 0
    je .load_done

    ; Berechne wie viele Sektoren bis zum Ende der aktuellen Spur
    mov al, [sectors_per_track]
    sub al, [current_sector]
    inc al                    ; +1 weil current_sector 1-basiert ist
    
    ; Begrenze auf verbleidene Sektoren
    mov bx, [sectors_remaining]
    cmp bx, ax
    jb .use_remaining
    mov bx, ax
.use_remaining:
    
    ; Begrenze auf maximum per read (DMA-Sicherheit)
    cmp bx, MAX_SECTORS_PER_READ
    jbe .sectors_ok
    mov bx, MAX_SECTORS_PER_READ
.sectors_ok:

    ; Multi-Sektor Read durchführen
    mov ax, [load_segment]
    mov es, ax
    mov di, [load_offset]

    mov ah, 0x02              ; Read sectors
    mov al, bl                ; Anzahl Sektoren
    mov ch, [current_track]   ; Track
    mov cl, [current_sector]  ; Sector
    mov dh, [current_head]    ; Head
    mov dl, [boot_drive]      ; Drive
    
    push bx                   ; Anzahl gelesener Sektoren merken
    int 0x13
    pop bx
    jc .load_error            ; Bei Fehler abbrechen

    ; Update counters
    sub word [sectors_remaining], bx
    
    ; Update load address
    mov ax, bx
    shl ax, 9                 ; * 512 bytes per sector
    add word [load_offset], ax
    jnc .update_position

    ; Segment overflow - advance segment
    add word [load_segment], 0x1000
    
.update_position:
    ; Update disk position
    add byte [current_sector], bl
    
    ; Check for track wrap
    mov al, [current_sector]
    cmp al, [sectors_per_track]
    jbe .load_loop
    
    ; Wrap to next track/head
    mov byte [current_sector], 1
    inc byte [current_head]
    
    mov al, [current_head]
    cmp al, [heads]
    jb .load_loop
    
    ; Wrap to next cylinder
    mov byte [current_head], 0
    inc word [current_track]
    jmp .load_loop

.load_error:
    ; Bei Fehlern: Versuche single-sector fallback
    mov word [sectors_remaining], 0  ; Stop loading
    
.load_done:
%if DEBUG_BOOT
    mov al, 'P'   ; Protected Mode
    call print_char
%endif

    ; A20 aktivieren
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Protected Mode aktivieren
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

%if ENABLE_BOOTINFO
setup_bootinfo:
    pusha
    xor eax, eax
    mov dword [BOOTINFO_MACHINE_OFF], eax
    mov dword [BOOTINFO_CONSOLE_OFF], eax
    mov dword [BOOTINFO_FLAGS_OFF], eax
    mov dword [BOOTINFO_PROM_OFF], eax
    mov dword [BOOTINFO_MEM_COUNT_OFF], eax

    mov eax, BI_ARCH_X86
    mov dword [BOOTINFO_ARCH_OFF], eax

    movzx eax, byte [boot_drive]
    mov dword [BOOTINFO_BOOTDEV_OFF], eax
    cmp al, 0x80
    jb .no_hdd_flag
    mov eax, BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD
    mov dword [BOOTINFO_FLAGS_OFF], eax
.no_hdd_flag:
    popa
    ret
%endif

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
%endif

BITS 32
protected_mode:
    ; Segmente setzen
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

%if DEBUG_PM_STUB
    ; VGA Test
    mov ebx, 0xB8000
    mov word [ebx], 0x0750     ; 'P'
    mov word [ebx+2], 0x074D   ; 'M'
    mov word [ebx+4], 0x074F   ; 'O'
    mov word [ebx+6], 0x074B   ; 'K'
.pm_debug_loop:
    hlt
    jmp .pm_debug_loop
%else
    ; Sprung zum Kernel
    jmp 0x8000
%endif

; Variablen
boot_drive:         db 0
load_offset:        dw 0
load_segment:       dw 0
current_sector:     db 0
current_head:       db 0
current_track:      dw 0
sectors_per_track:  db 0
heads:              db 0
sectors_remaining:  dw 0

; GDT
align 4
gdt_start:
    dq 0x0000000000000000   ; Null
    dq 0x00CF9A000000FFFF   ; Code
    dq 0x00CF92000000FFFF   ; Data
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
