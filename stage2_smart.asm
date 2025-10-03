; stage2_smart.asm - Intelligentes progressives Loading
BITS 16
ORG 0x10000

%include "boot_shared.inc"

stage2_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov al, '2'
    call print_char

    mov dl, [BOOT_DRIVE_ADDR]
    mov [boot_drive], dl

    ; Lade genug vom Kernel für Funktionalität (ca. 150 Sektoren)
    mov word [load_segment], 0x0800
    mov word [load_offset], 0
    mov byte [current_sector], 3
    mov byte [current_head], 0
    mov word [current_cylinder], 0
    mov word [sectors_remaining], 150  ; Reduziert aber funktional

    mov al, 'K'
    call print_char

    ; Progressives Loading mit Feedback
    mov word [progress_counter], 0

.load_loop:
    cmp word [sectors_remaining], 0
    je .load_done

    ; Lade einen Sektor
    mov ax, [load_segment]
    mov es, ax
    mov bx, [load_offset]

    mov ah, 0x02
    mov al, 1
    mov ch, [current_cylinder]
    mov cl, [current_sector]
    mov dh, [current_head]
    mov dl, [boot_drive]
    int 0x13
    jc .load_done  ; Bei Fehler: Stoppen

    ; Progress
    inc word [progress_counter]
    mov ax, [progress_counter]
    test ax, 15  ; Alle 16 Sektoren
    jnz .no_progress
    mov al, '.'
    call print_char
.no_progress:

    ; Update
    dec word [sectors_remaining]
    add word [load_offset], 512
    jnc .advance_chs
    add word [load_segment], 0x1000
    mov word [load_offset], 0

.advance_chs:
    inc byte [current_sector]
    cmp byte [current_sector], 18
    jbe .load_loop
    mov byte [current_sector], 1
    inc byte [current_head]
    cmp byte [current_head], 2
    jb .load_loop
    mov byte [current_head], 0
    inc word [current_cylinder]
    jmp .load_loop

.load_done:
    mov al, 'L'
    call print_char

    ; A20 aktivieren
    in al, 0x92
    or al, 2
    out 0x92, al

    mov al, 'P'
    call print_char

    ; Protected Mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

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

BITS 32
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x9FC00

    ; Sprung zum Kernel (das sollte jetzt funktionieren)
    jmp 0x8000

; Variablen
boot_drive:         db 0
load_offset:        dw 0
load_segment:       dw 0
current_sector:     db 0
current_head:       db 0
current_cylinder:   dw 0
sectors_remaining:  dw 0
progress_counter:   dw 0

align 4
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

times 512-($-$$) db 0
