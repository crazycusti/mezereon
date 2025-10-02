; stage2.asm - Zweiter Stage des Bootloaders
BITS 16
ORG 0x7E00

%include "boot_shared.inc"

%define DEBUG_BOOT 1
%define KERNEL_SECTORS 60   ; Maximale Kernel-Größe in Sektoren

stage2_start:
%if DEBUG_BOOT
    mov al, '2'       ; Stage 2 gestartet
    call print_char
%endif

    xor ax, ax
    mov ds, ax

    ; Boot-Drive von Stage 1
    mov dl, [BOOT_DRIVE_ADDR]
    mov [boot_drive], dl

%if ENABLE_BOOTINFO
    call setup_bootinfo
%endif

    ; Kernel-Load-Position: 0x8000
    mov word [load_segment], 0x0800
    mov word [load_offset], 0

    ; Disk Parameter abfragen
    mov ah, 0x08
    mov dl, [boot_drive]
    int 0x13
    jc .use_defaults

    ; Parameter speichern
    mov [max_head], dh
    mov al, cl
    and al, 0x3F      ; Nur Sektoren
    mov [spt], al
    jmp .check_params

.use_defaults:
    ; Standard Floppy Parameter
    mov byte [spt], 18
    mov byte [max_head], 1

.check_params:
    ; Parameter validieren
    cmp byte [spt], 0
    jnz .head_ok
    mov byte [spt], 18
.head_ok:
    cmp byte [max_head], 0
    jnz .params_ok
    mov byte [max_head], 1

.params_ok:
    ; Startposition: Sektor 3 (nach Stage 1+2)
    mov byte [sect], 3
    mov byte [head], 0
    mov word [cyl], 0
    mov word [remain], KERNEL_SECTORS

.load_loop:
    cmp word [remain], 0
    je .load_done

    mov si, 3

.retry:
    mov ax, [load_segment]
    mov es, ax
    mov bx, [load_offset]

    mov ax, [cyl]     ; Low 8 bits von Cylinder
    mov ch, al
    mov cl, [sect]    ; Sektor in CL[0:5]
    mov dh, [head]
    mov dl, [boot_drive]

    mov ah, [cyl+1]   ; High Bits von Cylinder
    and ah, 0x03
    shl ah, 6
    or cl, ah         ; In CL[6:7]

    mov ax, 0x0201    ; Read 1 sector
    int 0x13
    jnc .read_ok

    ; Reset und neu versuchen
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    dec si
    jnz .retry
    jmp error

.read_ok:
    dec word [remain]

    mov ax, [load_offset]
    add ax, 512
    mov [load_offset], ax
    jnc .advance_chs

    mov ax, [load_segment]
    add ax, 0x1000
    mov [load_segment], ax

.advance_chs:
    inc byte [sect]
    mov al, [sect]
    cmp al, [spt]
    jbe .continue_load

    mov byte [sect], 1
    inc byte [head]
    mov al, [head]
    cmp al, [max_head]
    jbe .continue_load

    mov byte [head], 0
    inc word [cyl]

.continue_load:
    jmp .load_loop

.load_done:
%if DEBUG_BOOT
    mov al, 'K'      ; Kernel geladen
    call print_char
%endif

    ; Protected Mode vorbereiten
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

error:
%if DEBUG_BOOT
    mov al, 'F'      ; Fatal Error
    call print_char
%endif
    jmp $

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

    xor eax, eax
    mov al, [boot_drive]
    mov dword [BOOTINFO_BOOTDEV_OFF], eax
    cmp al, 0x80
    jb .no_hdd_flag
    mov ax, BOOTINFO_FLAG_BOOT_DEVICE_IS_HDD
    mov dword [BOOTINFO_FLAGS_OFF], eax
.no_hdd_flag:
    popa
    ret
%endif

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
    
    ; Zum Kernel springen
    jmp 0x8000

; Variablen
boot_drive:   db 0
load_offset:  dw 0
load_segment: dw 0
sect:         db 0
head:         db 0
cyl:          dw 0
spt:          db 0
max_head:     db 0
remain:       dw 0

; GDT
align 8
gdt_start:
    dq 0x0000000000000000   ; Null Descriptor
    dq 0x00CF9A000000FFFF   ; Code Descriptor
    dq 0x00CF92000000FFFF   ; Data Descriptor
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
