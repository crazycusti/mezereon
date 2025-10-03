; stage2_full_progressive.asm - Vollständiges Kernel-Loading mit Progress
BITS 16
ORG 0x10000

%include "boot_shared.inc"

%ifndef DEBUG_BOOT
%define DEBUG_BOOT 1
%endif
%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 210
%endif

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

    ; Kernel-Ladeposition
    mov word [load_segment], 0x0800
    mov word [load_offset], 0
    
    ; CHS-Position
    mov byte [current_sector], 3
    mov byte [current_head], 0
    mov word [current_cylinder], 0
    mov word [sectors_remaining], KERNEL_SECTORS

    mov al, 'K'
    call print_char

    ; Schnelles Multi-Sektor Loading
.load_loop:
    cmp word [sectors_remaining], 0
    je .load_done

    ; Lade bis zu 18 Sektoren (eine ganze Spur)
    mov ax, [sectors_remaining]
    cmp ax, 18
    jbe .load_count_ok
    mov ax, 18
.load_count_ok:
    mov [sectors_to_read], ax

    ; Setup für Read
    mov bx, [load_segment]
    mov es, bx
    mov bx, [load_offset]

    mov ah, 0x02
    mov al, [sectors_to_read]     ; Anzahl Sektoren
    mov ch, [current_cylinder]    ; Cylinder
    mov cl, [current_sector]      ; Sector
    mov dh, [current_head]        ; Head
    mov dl, [boot_drive]
    int 0x13
    jc .load_single               ; Bei Fehler: Single-Sektor-Fallback

    ; Update nach erfolgreichem Multi-Read
    mov ax, [sectors_to_read]
    sub word [sectors_remaining], ax
    
    ; Update load address
    shl ax, 9                     ; * 512
    add word [load_offset], ax
    jnc .update_chs
    add word [load_segment], 0x1000
    mov word [load_offset], 0

.update_chs:
    ; Update CHS
    mov al, [sectors_to_read]
    add byte [current_sector], al
    
    ; Check Spur-Ende
    cmp byte [current_sector], 18
    jbe .load_loop
    
    ; Nächste Spur
    sub byte [current_sector], 18
    inc byte [current_head]
    cmp byte [current_head], 2
    jb .load_loop
    
    ; Nächster Cylinder
    mov byte [current_head], 0
    inc word [current_cylinder]
    jmp .load_loop

.load_single:
    ; Fallback: Single-Sektor
    mov ah, 0x02
    mov al, 1
    int 0x13
    jc .load_done   ; Bei weiterem Fehler: Aufhören
    
    dec word [sectors_remaining]
    add word [load_offset], 512
    jnc .single_advance
    add word [load_segment], 0x1000
    mov word [load_offset], 0
    
.single_advance:
    inc byte [current_sector]
    jmp .load_loop

.load_done:
    mov al, 'L'   ; Loading complete
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

    ; VGA Success
    mov ebx, 0xB8000
    mov word [ebx+0], 0x4F4B    ; 'K' weiß auf rot
    mov word [ebx+2], 0x4F52    ; 'R' 
    mov word [ebx+4], 0x4F4E    ; 'N'
    mov word [ebx+6], 0x4F4C    ; 'L' - Kernel loaded

    ; Sprung zum Kernel
    jmp 0x8000

; Variablen
boot_drive:         db 0
load_offset:        dw 0
load_segment:       dw 0
current_sector:     db 0
current_head:       db 0
current_cylinder:   dw 0
sectors_remaining:  dw 0
sectors_to_read:    dw 0

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
