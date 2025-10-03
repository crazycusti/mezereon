; stage2.asm - Reparierter Stage 2 mit Kernel-Loading
BITS 16
ORG 0x10000    ; Neue Ladeadresse!

%include "boot_shared.inc"

; Defaults falls nicht von Makefile gesetzt
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

stage2_start:
    ; Segmente und Stack neu initialisieren
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

    ; Kernel-Ladeposition: 0x8000 (original)
    mov word [load_segment], 0x0800
    mov word [load_offset], 0

    ; Disk-Parameter abfragen (einfach)
    mov byte [spt], 18
    mov byte [max_head], 1

    ; Startposition: Sektor 3 (nach Stage 1+2)
    mov byte [sect], 3
    mov byte [head], 0
    mov word [cyl], 0
    mov word [remain], KERNEL_SECTORS

%if DEBUG_BOOT
    mov al, 'K'   ; Kernel loading
    call print_char
%endif

    ; Kernel-Loading-Loop (vereinfacht)
.load_loop:
    cmp word [remain], 0
    je .load_done

    ; Kernel-Sektor laden
    mov ax, [load_segment]
    mov es, ax
    mov bx, [load_offset]

    mov ah, 0x02
    mov al, 1        ; 1 Sektor
    mov ch, [cyl]    ; Cylinder
    mov cl, [sect]   ; Sektor
    mov dh, [head]   ; Head
    mov dl, [boot_drive]
    int 0x13
    jc .load_done    ; Bei Fehler abbrechen

    ; Nächsten Sektor vorbereiten
    dec word [remain]
    add word [load_offset], 512
    jnc .advance_chs

    ; Segment erweitern bei Überlauf
    add word [load_segment], 0x1000
    mov word [load_offset], 0

.advance_chs:
    inc byte [sect]
    mov al, [sect]
    cmp al, [spt]
    jbe .load_loop

    mov byte [sect], 1
    inc byte [head]
    mov al, [head]
    cmp al, [max_head]
    jbe .load_loop

    mov byte [head], 0
    inc word [cyl]
    jmp .load_loop

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

    xor eax, eax
    mov al, [boot_drive]
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
    ; VGA Test Output
    mov ebx, 0xB8000
    mov word [ebx], 0x0750     ; 'P'
    mov word [ebx+2], 0x074D   ; 'M'
    mov word [ebx+4], 0x074F   ; 'O'
    mov word [ebx+6], 0x074B   ; 'K'
.pm_debug_loop:
    hlt
    jmp .pm_debug_loop
%else
    ; Sprung zum Kernel (0x8000)
    jmp 0x8000
%endif

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

; GDT (relativ zur neuen Adresse)
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
