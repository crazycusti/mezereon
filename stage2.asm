; stage2.asm - Zweiter Stage des Bootloaders
BITS 16
ORG 0x7E00

%define DEBUG_BOOT 1
%define KERNEL_SECTORS 60   ; Maximale Kernel-Größe in Sektoren

stage2_start:
%if DEBUG_BOOT
    mov al, '2'       ; Stage 2 gestartet
    call print_char
%endif

    ; Boot-Drive von Stage 1
    mov dl, [0x7C00 + boot_drive_offset]

    ; Kernel-Load-Position: 0x8000
    mov ax, 0x0800
    mov es, ax
    xor bx, bx        ; ES:BX = 0x8000

    ; Disk Parameter abfragen
    mov ah, 0x08
    int 0x13
    jc .use_defaults

    ; Parameter speichern
    mov [max_head], dh
    and cl, 0x3F      ; Nur Sektoren
    mov [spt], cl
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

    mov ch, [cyl]     ; Low 8 bits von Cylinder
    mov cl, [sect]    ; Sektor in CL[0:5]
    mov dh, [head]
    mov dl, [0x7C00 + boot_drive_offset]

    mov bl, [cyl+1]   ; High Bits von Cylinder
    shl bl, 6
    or cl, bl         ; In CL[6:7]

    mov al, 1         ; Ein Sektor
    mov ah, 0x02      ; Read Sectors
    mov bx, 0         ; Offset im Segment
    mov cx, 3         ; 3 Versuche

.retry:
    pusha
    int 0x13
    popa
    jnc .read_ok

    ; Reset und neu versuchen
    dec cx
    jz error
    push ax
    xor ax, ax
    int 0x13
    pop ax
    jmp .retry

.read_ok:
    ; Nächster Sektor
    add bx, 512      ; Buffer + 512
    jnc .no_inc_es
    mov ax, es
    add ax, 0x1000
    mov es, ax
    xor bx, bx
.no_inc_es:
    dec word [remain]

    ; Nächste CHS-Position
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
    mov al, 'K'      ; Kernel geladen
    call print_char
%endif

    ; Protected Mode vorbereiten
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:protected_mode

error:
%if DEBUG_BOOT
    mov al, 'F'      ; Fatal Error
    call print_char
%endif
    jmp $

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
sect:     db 0
head:     db 0
cyl:      dw 0
spt:      db 0
max_head: db 0
remain:   dw 0

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

boot_drive_offset equ 0x1F   ; Offset von boot_drive in stage1

times 512-($-$$) db 0
