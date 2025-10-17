; stage2.asm - Extended Stage 2 loader with CHS/LBA support
BITS 16
ORG 0

%include "boot_config.inc"
%include "boot_shared.inc"

%ifndef STAGE2_DEBUG
%define STAGE2_DEBUG 0
%endif

%ifndef ENABLE_A20_KBC
%define ENABLE_A20_KBC 1
%endif

%ifndef DEBUG_PM_STUB
%define DEBUG_PM_STUB 0
%endif


%ifndef STAGE3_SECTORS
%error "STAGE3_SECTORS must be defined via the build system"
%endif

%define CODE_SEL                 0x08
%define DATA_SEL                 0x10
%define MAX_LBA_CHUNK            127
%define E820_MAX_ENTRIES         32
%define STAGE3_SIGNATURE_LEN     7
%define STAGE2_LINEAR_LO         (STAGE2_LINEAR_ADDR & 0xFFFF)
%define STAGE2_LINEAR_HI         ((STAGE2_LINEAR_ADDR >> 16) & 0xFFFF)

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

    mov [boot_drive], dl
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_s2_start
    call debug_print_str
    mov al, [boot_drive]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif

%if STAGE2_DEBUG
    mov al, '2'
    call debug_char
%endif

    call detect_disk_extensions
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_use_lba
    call debug_print_str
    mov al, [use_lba]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif
    call query_geometry

    mov dword [current_lba], STAGE3_START_SECTOR
    mov word [remaining_sectors], STAGE3_SECTORS
    mov dword [buffer_linear], STAGE3_LINEAR_ADDR
    call load_sectors

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

    ; --- Probe current BIOS video mode (INT 10h AH=0x0F) and VBE ModeInfo
    push ax
    push bx
    push cx
    push dx
    ; get current BIOS mode
    mov ah, 0x0F
    int 0x10
    movzx eax, al
    ; write 16-bit mode into BOOTINFO VBE mode offset
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_MODE
    mov [es:di], al
    mov [es:di+1], ah

    ; prepare scratch buffer at BOOTINFO_ADDR+0x20
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    add di, 0x20
    mov word [es:di], 0
    mov word [es:di+2], 0

    ; probe VBE controller
    mov ax, 0x4F00
    int 0x10
    jc .vbe_skip
    cmp ax, 0x004F
    jne .vbe_skip

    ; Read VideoModePtr (offset 0x0E) from controller info at ES:DI
    mov si, [es:di+0x0E]
    mov dx, [es:di+0x10]

.vbe_mode_iter:
    ; load mode word from DX:SI
    push es
    push ds
    mov ax, dx
    mov ds, ax
    mov bx, si
    mov ax, [ds:bx]
    cmp ax, 0xFFFF
    je .vbe_done

    mov cx, ax            ; CX = mode number
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    add di, 0x20
    mov ax, 0x4F01
    int 0x10
    jc .vbe_skip_one
    cmp ax, 0x004F
    jne .vbe_skip_one

    ; parse ModeInfo: bytes_per_scanline @+0x10, xres @+0x12, yres @+0x14, bpp @+0x19, physbase @+0x28
    mov ax, [es:di+0x10]
    mov bx, BOOTINFO_SEGMENT
    mov es, bx
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_PITCH
    mov [es:di], ax

    mov ax, [es:di- (BOOTINFO_VBE_PITCH - 0x12) ]
    ; store width
    mov ax, [es:di+2]
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_WIDTH
    mov [es:di], ax
    ; store height
    mov ax, [es:di+2]
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_HEIGHT
    mov [es:di], ax
    ; store bpp
    mov al, [es:di+4]
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_BPP
    mov [es:di], al
    ; store physbase dword (at +0x28)
    mov bx, [es:di+0x12]
    mov cx, [es:di+0x14]
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_FB_ADDR
    mov [es:di], bx
    mov [es:di+2], cx

.vbe_skip_one:
    pop ds
    pop es
    add si, 2
    jmp .vbe_mode_iter
.vbe_done:
.vbe_skip:
    pop dx
    pop cx
    pop bx
    pop ax

    call preload_kernel_if_needed

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
    mov si, msg_stage3_ptr
    call debug_print_str
    mov ax, [stage3_params_ptr]
    mov dx, [stage3_params_ptr + 2]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
    mov si, msg_stage3_head0
    call debug_print_str
    push es
    mov ax, STAGE3_LOAD_SEGMENT
    mov es, ax
    mov ax, [es:0]
    mov dx, [es:2]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
    mov si, msg_stage3_head4
    call debug_print_str
    mov ax, [es:4]
    mov dx, [es:6]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
    pop es
%endif

    call ensure_a20

%if STAGE2_VERBOSE_DEBUG
    mov si, msg_sel_code
    call debug_print_str
    mov ax, CODE_SEL
    call debug_print_hex16
    mov si, msg_sel_data
    call debug_print_str
    mov ax, DATA_SEL
    call debug_print_hex16
    mov si, msg_newline
    call debug_print_str
    mov si, msg_gdt_code_hdr
    call debug_print_str
    mov bx, gdt_code
    call dump_descriptor
    mov si, msg_gdt_data_hdr
    call debug_print_str
    mov bx, gdt_data
    call dump_descriptor
%endif

    cli
    call load_gdt
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_gdt_lim
    call debug_print_str
    mov ax, [gdt_descriptor]
    call debug_print_hex16
    mov si, msg_newline
    call debug_print_str
    mov si, msg_gdt_base
    call debug_print_str
    mov ax, [gdt_descriptor + 2]
    mov dx, [gdt_descriptor + 4]
    call debug_print_hex32
    mov si, msg_far_bytes
    call debug_print_str
    mov bx, far_jmp_label
    mov cx, far_jmp_end - far_jmp_label
.far_bytes_loop:
    mov al, [bx]
    call debug_print_hex8
    inc bx
    loop .far_bytes_loop
    mov si, msg_far_sel
    call debug_print_str
    mov ax, CODE_SEL
    call debug_print_hex16
    mov si, msg_far_off
    call debug_print_str
    mov ax, pm_stub
    call debug_print_hex16
    mov si, msg_far_lin
    call debug_print_str
    mov ax, pm_stub
    mov dx, STAGE2_LINEAR_HI
    mov bx, STAGE2_LINEAR_LO
    add ax, bx
    adc dx, 0
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
%endif
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_stage3_head0
    call debug_print_str
    mov ax, STAGE3_LOAD_SEGMENT
    mov ds, ax
    mov al, [0]
    call debug_print_hex8
    mov al, [1]
    call debug_print_hex8
    mov al, [2]
    call debug_print_hex8
    mov al, [3]
    call debug_print_hex8
    mov al, [4]
    call debug_print_hex8
    mov al, [5]
    call debug_print_hex8
    mov al, [6]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif
    call check_stage3_signature
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_sig_ok
    test al, al
    jz .sig_ok_dbg
    mov si, msg_sig_bad
.sig_ok_dbg:
    call debug_print_str
    mov si, msg_newline
    call debug_print_str
%endif
    test al, al
    jnz .sig_fail
    jmp .after_sig
.sig_fail:
    jmp fatal_halt
.after_sig:
    mov eax, cr0
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_cr0b
    mov [tmp_dword], eax
    call debug_print_str
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
%endif
    or eax, 1
    mov cr0, eax
    mov esi, stage3_params
    add esi, STAGE2_LINEAR_ADDR
    mov edi, BOOTINFO_ADDR
far_jmp_label:
    jmp dword CODE_SEL:(STAGE2_LINEAR_ADDR + pm_stub)
far_jmp_end:

load_error:
    mov al, 'F'
    call debug_char
    mov al, ah
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
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

check_stage3_signature:
    push ds
    push si
    push bx
    push dx
    push cx
    mov ax, STAGE3_LOAD_SEGMENT
    mov ds, ax
    xor si, si
    xor bx, bx
    mov cx, STAGE3_SIGNATURE_LEN
.sig_loop:
    mov al, [si]
    mov dl, cs:[stage3_signature + bx]
    cmp al, dl
    jne .sig_bad
    inc si
    inc bx
    loop .sig_loop
    mov al, 0
    jmp .sig_done
.sig_bad:
    mov al, 1
.sig_done:
    pop cx
    pop dx
    pop bx
    pop si
    pop ds
    ret

dump_descriptor:
    push ax
    push dx
    push bx
    push cx
    push si
    push di
    mov di, bx
    mov si, msg_desc_raw
    call debug_print_str
    mov cx, 8
.raw_loop:
    mov al, [di]
    call debug_print_hex8
    mov al, ' '
    call debug_char
    inc di
    loop .raw_loop
    mov si, msg_desc_lim
    call debug_print_str
    mov ax, [bx]
    mov dx, 0
    mov dl, [bx + 6]
    and dl, 0x0F
    call debug_print_hex32
    mov si, msg_desc_base
    call debug_print_str
    mov ax, [bx + 2]
    mov dl, [bx + 4]
    mov dh, [bx + 7]
    call debug_print_hex32
    mov si, msg_desc_acc
    call debug_print_str
    mov al, [bx + 5]
    call debug_print_hex8
    mov si, msg_desc_flags
    call debug_print_str
    mov al, [bx + 6]
    and al, 0xF0
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
    pop di
    pop si
    pop cx
    pop bx
    pop dx
    pop ax
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
    cmp dl, 0x80
    jb .default_chs
    mov byte [use_lba], 1
    jmp .probe
.default_chs:
    mov byte [use_lba], 0
.probe:
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .check_result
    cmp bx, 0xAA55
    jne .check_result
    test cx, 1
    jz .check_result
    mov byte [use_lba], 1
    jmp .done
.check_result:
    mov dl, [boot_drive]
    cmp dl, 0x80
    jb .force_chs_only
    jmp .done
.force_chs_only:
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
    cmp word [e820_entry_count], 0
    je .skip_e820_details
    mov si, msg_e820_base
    call debug_print_str
    mov ax, [e820_entries]
    mov dx, [e820_entries + 2]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
    mov si, msg_e820_len
    call debug_print_str
    mov ax, [e820_entries + 8]
    mov dx, [e820_entries + 10]
    call debug_print_hex32
    mov si, msg_newline
    call debug_print_str
.skip_e820_details:
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
    call disk_wait_settle
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
    mov bp, 3
    mov ax, [chunk_size]
    cmp ax, 1
    jne .shrink_chunk
    mov byte [use_lba], 1
    stc
    jmp .exit
.shrink_chunk:
    shr ax, 1
    cmp ax, 1
    jae .store_shrunk
    mov ax, 1
.store_shrunk:
    mov [chunk_size], ax
    jmp .read_retry
.success:
    clc
.exit:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

load_sectors:
    cmp word [remaining_sectors], 0
    je .done
.loop:
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_load_loop
    call debug_print_str
    mov ax, [remaining_sectors]
    call debug_print_hex16
    mov si, msg_newline
    call debug_print_str
%endif
    movzx eax, word [remaining_sectors]
    cmp eax, MAX_LBA_CHUNK
    jbe .chunk_ok
    mov eax, MAX_LBA_CHUNK
.chunk_ok:
    mov word [chunk_size], ax
    mov word [chunk_size_initial], ax

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
    jc .chs_failed
    jmp .after_read

.chs_failed:
    cmp byte [use_lba], 0
    jne .chs_try_lba
    mov si, msg_err_chs
    call debug_print_str
    mov si, msg_newline
    call debug_print_str
    jmp load_error
.chs_try_lba:
    mov ax, [chunk_size_initial]
    mov [chunk_size], ax
    call read_chunk_lba
    jc .chs_lba_fail
    jmp .after_read
.chs_lba_fail:
    mov si, msg_err_lba
    call debug_print_str
    mov si, msg_newline
    call debug_print_str
    jmp load_error

.read_lba:
    call read_chunk_lba
    jc .lba_fail
    jmp .after_read
.lba_fail:
    mov si, msg_err_lba
    call debug_print_str
    mov si, msg_newline
    call debug_print_str
    jmp load_error

.after_read:
    mov si, word [chunk_size]
    call advance_buffer
    call advance_lba

    mov ax, [remaining_sectors]
    sub ax, [chunk_size]
    mov [remaining_sectors], ax
    cmp ax, 0
    jne .loop
.done:
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
    call disk_wait_settle
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

; Give floppy drives ~200ms to settle between BIOS disk calls (fallback if INT 15h fails)
disk_wait_settle:
    push ax
    push bx
    push cx
    push dx
    mov dl, [boot_drive]
    cmp dl, 0x80
    jae .skip_wait
    mov ax, 0x8600
    mov cx, 0x0003
    mov dx, 0x0D40
    int 0x15
    jnc .done
    mov cx, 0x0020
.busy_outer:
    mov bx, 0xFFFF
.busy_inner:
    dec bx
    jnz .busy_inner
    loop .busy_outer
.done:
.skip_wait:
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
    call disk_wait_settle
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
    mov eax, KERNEL_BUFFER_LINEAR
    mov [stage3_params + STAGE3_PARAM_KERNEL_BUFFER], eax
    mov eax, e820_entries
    add eax, STAGE2_LINEAR_ADDR
    mov [stage3_params + STAGE3_PARAM_E820_PTR], eax
    movzx eax, word [e820_entry_count]
    mov [stage3_params + STAGE3_PARAM_E820_COUNT], eax
    mov eax, stage3_params
    add eax, STAGE2_LINEAR_ADDR
    mov [stage3_params_ptr], eax
    ret

; Preload kernel image when booting from floppy (fallback for Stage 3)
preload_kernel_if_needed:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov al, [boot_drive]
    cmp al, 0x80
    jae .skip
    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_SECTORS]
    mov dx, [stage3_params + STAGE3_PARAM_KERNEL_SECTORS + 2]
    or ax, dx
    jz .skip

    mov ax, [buffer_linear]
    push ax
    mov ax, [buffer_linear + 2]
    push ax
    mov ax, [current_lba]
    push ax
    mov ax, [current_lba + 2]
    push ax

    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_BUFFER]
    mov [buffer_linear], ax
    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_BUFFER + 2]
    mov [buffer_linear + 2], ax

    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_LBA]
    mov [current_lba], ax
    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_LBA + 2]
    mov [current_lba + 2], ax

    mov ax, [stage3_params + STAGE3_PARAM_KERNEL_SECTORS]
    mov [remaining_sectors], ax

    call load_sectors

    mov eax, [stage3_params + STAGE3_PARAM_FLAGS]
    or eax, STAGE3_FLAG_KERNEL_PRELOADED
    mov [stage3_params + STAGE3_PARAM_FLAGS], eax

    mov word [remaining_sectors], 0

    pop ax
    mov [current_lba + 2], ax
    pop ax
    mov [current_lba], ax
    pop ax
    mov [buffer_linear + 2], ax
    pop ax
    mov [buffer_linear], ax

.skip:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; Ensure A20 gate enabled via Fast A20 toggle with keyboard controller fallback
ensure_a20:
    push ax
    push dx
    mov cx, 3
.fast_retry:
    call enable_a20_fast
    call test_a20_wrap
    cmp al, 0
    jne .done
    loop .fast_retry
%if ENABLE_A20_KBC
    mov cx, 5
.kbc_retry:
    call enable_a20_kbc
    call test_a20_wrap
    cmp al, 0
    jne .done
    loop .kbc_retry
%endif
    jmp .fatal
.done:
    pop dx
    pop ax
    ret
.fatal:
    pop dx
    pop ax
    jmp fatal_halt

test_a20_wrap:
    push bx
    push ds
    push es
    push si
    push di
    xor ax, ax
    mov ds, ax
    mov si, 0x0500
    mov ax, 0xFFFF
    mov es, ax
    mov di, 0x0510
    mov bl, [ds:si]
    mov bh, [es:di]
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    cmp byte [ds:si], 0xFF
    mov byte [es:di], bh
    mov byte [ds:si], bl
    pop di
    pop si
    pop es
    pop ds
    pop bx
    jne .a20_enabled
    xor ax, ax
    ret
.a20_enabled:
    mov ax, 1
    ret

%if STAGE2_VERBOSE_DEBUG
report_a20:
    push ax
    cmp al, 0
    jne .a20_on_msg
    mov si, msg_a20_off
    call debug_print_str
    jmp .report_exit
.a20_on_msg:
    mov si, msg_a20_on
    call debug_print_str
.report_exit:
    mov si, msg_newline
    call debug_print_str
    pop ax
    ret
%endif

enable_a20_fast:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

%if ENABLE_A20_KBC
enable_a20_kbc:
    push ax
    call enable_a20_wait_ibf_clear
    mov al, 0xAD             ; disable keyboard
    out 0x64, al
    call enable_a20_wait_ibf_clear
    mov al, 0xD0             ; read output port
    out 0x64, al
    call enable_a20_wait_obf_set
    in al, 0x60
    or al, 0x02              ; set A20 bit
    mov ah, al
    call enable_a20_wait_ibf_clear
    mov al, 0xD1             ; write output port
    out 0x64, al
    call enable_a20_wait_ibf_clear
    mov al, ah
    out 0x60, al
    call enable_a20_wait_ibf_clear
    mov al, 0xAE             ; re-enable keyboard
    out 0x64, al
    call enable_a20_wait_ibf_clear
    pop ax
    ret

enable_a20_wait_ibf_clear:
    in al, 0x64
    test al, 0x02
    jnz enable_a20_wait_ibf_clear
    ret

enable_a20_wait_obf_set:
    in al, 0x64
    test al, 0x01
    jz enable_a20_wait_obf_set
    ret
%endif

; Load GDT descriptor with runtime base
load_gdt:
    mov word [gdt_descriptor], gdt_end - gdt_start - 1
    mov eax, gdt_start
    add eax, STAGE2_LINEAR_ADDR
    mov [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]
    ret

; -----------------------------------------------------------------------------
; Data
; -----------------------------------------------------------------------------

msg_s2_start:        db 'S2 INIT: drive=0x',0
msg_use_lba:         db 'use_lba=0x',0
hex_digits:          db '0123456789ABCDEF'
msg_load_loop:       db 'Loading stage3 chunk, remaining=',0
msg_chs_prefix:      db 'CHS C=',0
msg_chs_head:        db ' H=',0
msg_chs_sector:      db ' S=',0
msg_count_prefix:    db ' count=',0
msg_lba_prefix:      db 'LBA=',0
msg_stage2_done:     db 'Stage2 loaded stage3',0
msg_e820_prefix:     db 'E820 entries=',0
msg_e820_base:       db 'E820[0]=0x',0
msg_e820_len:        db 'E820len=0x',0
msg_params_drive:    db 'S3 d=0x',0
msg_params_lba:      db ' lba=0x',0
msg_params_stage3:   db ' s3s=0x',0
msg_params_kernel_l: db ' klba=0x',0
msg_params_kernel_s: db ' ksec=0x',0
msg_params_flags:    db ' flag=0x',0
msg_stage3_ptr:      db ' s3ptr=0x',0
msg_stage3_head0:    db ' S3[0]=0x',0
msg_stage3_head4:    db ' S3[4]=0x',0
msg_sig_ok:          db 'S3sig=OK',0
msg_sig_bad:         db 'S3sig=BAD',0
msg_gdt_code_hdr:    db 'GDTc',0
msg_gdt_data_hdr:    db 'GDTd',0
msg_desc_raw:        db ' raw=',0
msg_desc_lim:        db ' lim=0x',0
msg_desc_base:       db ' base=0x',0
msg_desc_acc:        db ' acc=0x',0
msg_desc_flags:      db ' flg=0x',0
msg_gdt_lim:         db 'GDTRlim=0x',0
msg_gdt_base:        db ' GDTRbase=0x',0
msg_cr0b:            db 'CR0b=0x',0
msg_cr0a:            db ' CR0a=0x',0
msg_pm_jump:         db 'S2:PM',0
msg_a20_on:          db 'A20=1',0
msg_a20_off:         db 'A20=0',0
msg_err_chs:         db 'CHS!',0
msg_err_lba:         db 'LBA!',0
msg_sel_code:        db 'SELc=0x',0
msg_sel_data:        db ' SELd=0x',0
msg_far_bytes:       db 'JMP=',0
msg_far_sel:         db ' sel=0x',0
msg_far_off:         db ' off=0x',0
msg_far_lin:         db ' lin=0x',0
msg_newline:         db 0x0D,0x0A,0

; VBE bootlog messages removed (kernel will log bootinfo)

stage3_signature:    db 0xB8,0x21,0x47,0x53,0x33,0x31,0xC0

boot_drive:           db 0
use_lba:              db 0
remaining_sectors:    dw 0
chunk_size:           dw 0
chunk_size_initial:   dw 0
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
align 4
tmp_dword:            dd 0

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
    dw 0x0017
    dd 0

; *** Stage 2 end

[BITS 32]
pm_stub:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, STAGE3_STACK_TOP
%if DEBUG_PM_STUB
    mov word [0xB8000], 0x0750        ; 'P'
    mov word [0xB8002], 0x0721        ; '!'
    mov dx, 0xE9
    mov al, 'P'
    out dx, al
    mov al, '!'
    out dx, al
%endif
    jmp 0x08:STAGE3_LINEAR_ADDR
[BITS 16]
