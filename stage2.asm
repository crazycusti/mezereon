; stage2.asm - Extended Stage 2 loader with CHS/LBA support
BITS 16
ORG 0

%include "boot_config.inc"
%include "boot_shared.inc"

%ifndef STAGE2_DEBUG
%define STAGE2_DEBUG 1
%endif

%ifndef STAGE3_SECTORS
%error "STAGE3_SECTORS must be defined via the build system"
%endif

%define BOOT_DRIVE_PTR           (0x7C00 + 0x1F)
%define CODE_SEL                 0x08
%define DATA_SEL                 0x10
%define MAX_LBA_CHUNK            127
%define E820_MAX_ENTRIES         32

%ifndef STAGE2_FORCE_CHS
%define STAGE2_FORCE_CHS 0
%endif

start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    mov ax, cs
    mov ds, ax
    mov es, ax
    sti

    mov dl, [BOOT_DRIVE_PTR]
    mov [boot_drive], dl

%if STAGE2_DEBUG
    mov al, '2'
    call debug_char
%endif

    call detect_disk_extensions
    call query_geometry

    mov dword [current_lba], STAGE3_START_SECTOR
    mov word [remaining_sectors], STAGE3_SECTORS
    mov dword [buffer_linear], STAGE3_LINEAR_ADDR

    cmp word [remaining_sectors], 0
    je load_complete

load_loop:
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_load_loop
    call debug_print_str
    mov ax, [remaining_sectors]
    call debug_print_hex16
    mov si, msg_newline
    call debug_print_str
%endif
    movzx eax, word [remaining_sectors]
    test eax, eax
    je load_complete

    cmp eax, MAX_LBA_CHUNK
    jbe .chunk_ok
    mov eax, MAX_LBA_CHUNK
.chunk_ok:
    mov word [chunk_size], ax

    cmp byte [use_lba], 0
    jne .prepare_transfer

    mov si, word [chunk_size]
    call limit_chunk_to_track
    mov [chunk_size], si

.prepare_transfer:
    call prepare_transfer_pointer

    cmp byte [use_lba], 0
    jne .read_lba
    call read_chunk_chs
    jc load_error
    jmp .after_read

.read_lba:
    call read_chunk_lba
    jc load_error

.after_read:
    mov si, word [chunk_size]
    call advance_buffer
    call advance_lba

    mov ax, [remaining_sectors]
    sub ax, [chunk_size]
    mov [remaining_sectors], ax
    jmp load_loop

load_complete:
%if STAGE2_DEBUG
    mov al, '3'
    call debug_char
%endif

%if STAGE2_VERBOSE_DEBUG
    mov si, msg_stage2_done
    call debug_print_str
    mov si, msg_newline
    call debug_print_str
%endif

    call collect_e820
    call populate_stage3_params

%if STAGE2_VERBOSE_DEBUG
    mov si, msg_params_drive
    call debug_print_str
    mov al, [stage3_params + STAGE3_PARAM_BOOT_DRIVE]
    call debug_print_hex8
    mov si, msg_params_lba
    call debug_print_str
    mov ax, [stage3_params + STAGE3_PARAM_STAGE3_LBA]
    mov dx, [stage3_params + STAGE3_PARAM_STAGE3_LBA + 2]
    call debug_print_hex32
    mov si, msg_params_stage3
    call debug_print_str
    mov ax, [stage3_params + STAGE3_PARAM_STAGE3_SECTORS]
    mov dx, [stage3_params + STAGE3_PARAM_STAGE3_SECTORS + 2]
    call debug_print_hex32
    mov si, msg_params_kernel_l
    call debug_print_str
    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_LBA]
    mov dx, [stage3_params + STAGE3_PARAM_KERNEL_LBA + 2]
    call debug_print_hex32
    mov si, msg_params_kernel_s
    call debug_print_str
    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_SECTORS]
    mov dx, [stage3_params + STAGE3_PARAM_KERNEL_SECTORS + 2]
    call debug_print_hex32
    mov si, msg_params_flags
    call debug_print_str
    mov ax, [stage3_params + STAGE3_PARAM_FLAGS]
    mov dx, [stage3_params + STAGE3_PARAM_FLAGS + 2]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
%endif

    ; Prepare registers for Stage 3 hand-off
    mov esi, dword [stage3_params_ptr]
    mov edi, BOOTINFO_ADDR

    call ensure_a20

    cli
    call load_gdt
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword CODE_SEL:STAGE3_LINEAR_ADDR

load_error:
    mov al, 'F'
    call debug_char
fatal_halt:
    hlt
    jmp fatal_halt

; -----------------------------------------------------------------------------
; Helpers and BIOS interaction
; -----------------------------------------------------------------------------

; Output AL as character via BIOS teletype
debug_char:
    push ax
    push bx
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    pop bx
    pop ax
    ret

; Print ASCIIZ string at DS:SI
debug_print_str:
    push ax
    push si
.next_char:
    lodsb
    test al, al
    jz .done
    call debug_char
    jmp .next_char
.done:
    pop si
    pop ax
    ret

debug_print_hex_digit:
    push ax
    push bx
    xor bh, bh
    mov bl, al
    and bl, 0x0F
    mov al, [hex_digits + bx]
    call debug_char
    pop bx
    pop ax
    ret

; Expect value in AL
debug_print_hex8:
    push ax
    mov ah, al
    shr al, 4
    call debug_print_hex_digit
    mov al, ah
    and al, 0x0F
    call debug_print_hex_digit
    pop ax
    ret

; Expect value in AX
debug_print_hex16:
    push ax
    mov al, ah
    call debug_print_hex8
    pop ax
    call debug_print_hex8
    ret

; Expect value in DX:AX (DX high word, AX low word)
debug_print_hex32:
    push ax
    push dx
    mov ax, dx
    call debug_print_hex16
    pop dx
    pop ax
    call debug_print_hex16
    ret

; Detect INT 13h extensions (LBA support)
detect_disk_extensions:
    push ax
    push bx
    push cx
    push dx
%if STAGE2_FORCE_CHS
    mov byte [use_lba], 0
    jmp .done
%endif
    mov dl, [boot_drive]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .no_ext
    cmp bx, 0xAA55
    jne .no_ext
    test cx, 1
    jz .no_ext
    mov byte [use_lba], 1
    jmp .done
.no_ext:
    mov byte [use_lba], 0
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Query CHS geometry information for the boot drive
query_geometry:
    push ax
    push bx
    push cx
    push dx
    mov dl, [boot_drive]
    mov ah, 0x08
    int 0x13
    jc .defaults
    ; Bits 0-5 of CL => sectors per track
    mov bx, cx
    and bx, 0x3F
    mov [sectors_per_track], bx
    ; DH holds maximum head number (0-based)
    mov bl, dh
    inc bl
    movzx bx, bl
    mov [heads_per_cyl], bx
    jmp .done
.defaults:
    mov word [sectors_per_track], 18
    mov word [heads_per_cyl], 2
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Collect E820 memory map entries while still in real mode
collect_e820:
    pushad
    push es
    mov ax, cs
    mov es, ax
    mov di, e820_entries
    mov word [e820_entry_count], 0
    xor ebx, ebx
    mov edx, 0x534D4150
.loop:
    mov eax, 0x0000E820
    mov edx, 0x534D4150
    mov ecx, 24
    movzx edi, di
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp word [e820_entry_count], E820_MAX_ENTRIES
    jae .done
    inc word [e820_entry_count]
    mov ax, cx
    cmp ax, 20
    jae .size_ok
    mov ax, 24
.size_ok:
    cmp ax, 24
    jbe .size_use
    mov ax, 24
.size_use:
    add di, ax
    cmp ebx, 0
    jne .loop
.done:
    pop es
    popad
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_e820_prefix
    call debug_print_str
    mov al, [e820_entry_count]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif
    ret

; Ensure chunk does not cross a track boundary (CHS mode), SI holds chunk
limit_chunk_to_track:
    push ax
    push bx
    push cx
    push dx
    mov eax, [current_lba]
    call lba_to_chs
    xor bx, bx
    mov bl, [chs_sector]
    mov cx, [sectors_per_track]
    mov dx, cx
    sub dx, bx
    add dx, 1
    cmp si, dx
    jbe .ok
    mov si, dx
.ok:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Prepare ES:BX for the next transfer based on buffer_linear
prepare_transfer_pointer:
    mov eax, [buffer_linear]
    mov edx, eax
    and edx, 0x0F
    mov word [transfer_offset], dx
    mov edx, eax
    shr edx, 4
    mov word [transfer_segment], dx
    mov bx, [transfer_offset]
    mov dx, [transfer_segment]
    mov es, dx
    ret

; Advance buffer pointer by chunk_size * 512 bytes
advance_buffer:
    movzx eax, word [chunk_size]
    shl eax, 9
    add dword [buffer_linear], eax
    ret

; Advance current LBA by chunk_size sectors
advance_lba:
    movzx eax, word [chunk_size]
    add dword [current_lba], eax
    ret

; Convert current LBA (EAX) into CHS values stored in globals
lba_to_chs:
    push ebx
    push ecx
    push edx
    movzx ebx, word [sectors_per_track]
    movzx ecx, word [heads_per_cyl]
    test ebx, ebx
    jnz .have_spt
    mov ebx, 18
.have_spt:
    test ecx, ecx
    jnz .have_heads
    mov ecx, 2
.have_heads:
    imul ecx, ebx                     ; sectors per cylinder
    xor edx, edx
    div ecx                           ; eax = cylinder, edx = remainder
    mov [chs_cylinder], ax
    mov eax, edx
    xor edx, edx
    div ebx                           ; eax = head, edx = sector index (0-based)
    mov [chs_head], al
    mov eax, edx
    inc eax
    mov [chs_sector], al
    pop edx
    pop ecx
    pop ebx
    ret

; Read chunk via CHS (uses global chunk_size)
read_chunk_chs:
    push ax
    push bx
    push cx
    push dx
    mov eax, [current_lba]
    call lba_to_chs

    mov ax, [chs_cylinder]
    mov bx, ax
    mov ch, al
    mov cl, [chs_sector]
    shr bx, 8
    and bl, 0x03
    shl bl, 6
    or cl, bl
    mov dh, [chs_head]

    mov byte [chs_reg_ch], ch
    mov byte [chs_reg_cl], cl
    mov byte [chs_reg_dh], dh

%if STAGE2_VERBOSE_DEBUG
    mov si, msg_chs_prefix
    call debug_print_str
    mov ax, [chs_cylinder]
    call debug_print_hex16
    mov si, msg_chs_head
    call debug_print_str
    mov al, [chs_head]
    call debug_print_hex8
    mov si, msg_chs_sector
    call debug_print_str
    mov al, [chs_sector]
    call debug_print_hex8
    mov si, msg_count_prefix
    call debug_print_str
    mov al, [chunk_size]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif

    mov bp, 3
.read_retry:
    mov bx, [transfer_offset]
    mov dx, [transfer_segment]
    mov es, dx
    mov ch, [chs_reg_ch]
    mov cl, [chs_reg_cl]
    mov dh, [chs_reg_dh]
    mov dl, [boot_drive]
    mov ah, 0x02
    mov al, byte [chunk_size]
    int 0x13
    jnc .success
    call reset_disk
    dec bp
    jnz .read_retry
    stc
    jmp .exit
.success:
    clc
.exit:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Read chunk via LBA (uses Disk Address Packet)
read_chunk_lba:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax, [chunk_size]
    mov [dap_packet + 2], ax
    mov ax, [transfer_offset]
    mov [dap_packet + 4], ax
    mov ax, [transfer_segment]
    mov [dap_packet + 6], ax
    mov eax, [current_lba]
    mov [dap_packet + 8], eax
    mov dword [dap_packet + 12], 0
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_lba_prefix
    call debug_print_str
    mov ax, [current_lba]
    mov dx, [current_lba + 2]
    call debug_print_hex32
    mov si, msg_count_prefix
    call debug_print_str
    mov al, [chunk_size]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif
    mov bp, 3
.retry:
    mov dl, [boot_drive]
    mov si, dap_packet
    mov ah, 0x42
    int 0x13
    jnc .success
    call reset_disk
    dec bp
    jnz .retry
    stc
    jmp .done
.success:
    clc
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Reset disk subsystem after failure
reset_disk:
    push ax
    push dx
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    pop dx
    pop ax
    ret

; Populate Stage 3 parameter block and compute pointer
populate_stage3_params:
    movzx eax, byte [boot_drive]
    mov [stage3_params + STAGE3_PARAM_BOOT_DRIVE], eax
    mov eax, STAGE3_START_SECTOR
    mov [stage3_params + STAGE3_PARAM_STAGE3_LBA], eax
    mov eax, STAGE3_SECTORS
    mov [stage3_params + STAGE3_PARAM_STAGE3_SECTORS], eax
    movzx eax, byte [use_lba]
    and eax, 1
    mov [stage3_params + STAGE3_PARAM_FLAGS], eax
    mov ax, [sectors_per_track]
    mov [stage3_params + STAGE3_PARAM_GEOM_SPT], ax
    mov ax, [heads_per_cyl]
    mov [stage3_params + STAGE3_PARAM_GEOM_HEADS], ax
    mov eax, STAGE3_LINEAR_ADDR
    mov [stage3_params + STAGE3_PARAM_STAGE3_LOAD], eax
    mov eax, BOOTINFO_ADDR
    mov [stage3_params + STAGE3_PARAM_BOOTINFO_PTR], eax
    mov eax, KERNEL_START_SECTOR
    mov [stage3_params + STAGE3_PARAM_KERNEL_LBA], eax
    mov eax, KERNEL_SECTORS
    mov [stage3_params + STAGE3_PARAM_KERNEL_SECTORS], eax
    mov eax, KERNEL_LOAD_LINEAR
    mov [stage3_params + STAGE3_PARAM_KERNEL_LOAD], eax
    mov eax, e820_entries
    add eax, STAGE2_LINEAR_ADDR
    mov [stage3_params + STAGE3_PARAM_E820_PTR], eax
    movzx eax, word [e820_entry_count]
    mov [stage3_params + STAGE3_PARAM_E820_COUNT], eax
    mov eax, stage3_params
    add eax, STAGE2_LINEAR_ADDR
    mov [stage3_params_ptr], eax
    ret

; Ensure A20 gate enabled via Fast A20 toggle with keyboard controller fallback
ensure_a20:
    push ax
    push dx
    call enable_a20_fast
    call check_a20
    test al, 0x02
    jnz .done
    call enable_a20_kbc
    call check_a20
.done:
    pop dx
    pop ax
    ret

enable_a20_fast:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

enable_a20_kbc:
    push ax
.wait_input_clear:
    in al, 0x64
    test al, 0x02
    jne .wait_input_clear
    mov al, 0xD1
    out 0x64, al
.wait_input_clear2:
    in al, 0x64
    test al, 0x02
    jne .wait_input_clear2
    mov al, 0xDF
    out 0x60, al
    pop ax
    ret

check_a20:
    in al, 0x92
    ret

; Load GDT descriptor with runtime base
load_gdt:
    mov eax, gdt_start
    add eax, STAGE2_LINEAR_ADDR
    mov [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------

hex_digits:          db '0123456789ABCDEF'
msg_load_loop:       db 'Loading stage3 chunk, remaining=',0
msg_chs_prefix:      db 'CHS C=',0
msg_chs_head:        db ' H=',0
msg_chs_sector:      db ' S=',0
msg_count_prefix:    db ' count=',0
msg_lba_prefix:      db 'LBA=',0
msg_stage2_done:     db 'Stage2 loaded stage3',0
msg_e820_prefix:     db 'E820 entries=',0
msg_params_drive:    db 'S3 params: drive=',0
msg_params_lba:      db ' LBA=',0
msg_params_stage3:   db ' stage3_secs=',0
msg_params_kernel_l: db ' kernel_lba=',0
msg_params_kernel_s: db ' kernel_secs=',0
msg_params_flags:    db ' flags=',0
msg_newline:         db 0x0D,0x0A,0

boot_drive:           db 0
use_lba:              db 0
remaining_sectors:    dw 0
chunk_size:           dw 0
sectors_per_track:    dw 18
heads_per_cyl:        dw 2
current_lba:          dd 0
buffer_linear:        dd 0
transfer_offset:      dw 0
transfer_segment:     dw 0
chs_sector:           db 0
chs_head:             db 0
chs_cylinder:         dw 0
chs_reg_ch:           db 0
chs_reg_cl:           db 0
chs_reg_dh:           db 0
e820_entry_count:     dw 0
align 4
e820_entries:         times E820_MAX_ENTRIES * 24 db 0
stage3_params:        times STAGE3_PARAM_SIZE db 0
stage3_params_ptr:    dd 0

; Disk Address Packet for INT 13h Extensions
; Byte 0 = size (16), byte 1 = reserved
; words: sectors, offset, segment; qword: starting LBA
; final dword reserved
align 2
dap_packet:
    db 16, 0
    dw 0, 0, 0
    dd 0, 0

align 4
gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10011010b
    db 11001111b
    db 0x00
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 10010010b
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd 0

; *** Stage 2 end
