; stage2.asm - Extended Stage 2 loader with CHS/LBA support
BITS 16
ORG 0

%include "boot_config.inc"
%include "boot_shared.inc"

%ifndef STAGE2_DEBUG
%define STAGE2_DEBUG 0
%endif

%ifndef STAGE2_E820_DEBUG
%define STAGE2_E820_DEBUG 0
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

; VBE/LFB preferences (override via assembler -D VBE_PREF_* or VBE_ENABLE_LFB)
%ifndef VBE_PREF_WIDTH
%define VBE_PREF_WIDTH 640
%endif
%ifndef VBE_PREF_HEIGHT
%define VBE_PREF_HEIGHT 480
%endif
%ifndef VBE_PREF_BPP
%define VBE_PREF_BPP 8
%endif
%ifndef VBE_ENABLE_LFB
%define VBE_ENABLE_LFB 1
%endif

%define A20_STATE_UNKNOWN         0
%define A20_STATE_ENABLED         1
%define A20_STATE_UNAVAILABLE     2
%define A20_BIOS_UNKNOWN          0
%define A20_BIOS_SUPPORTED        1
%define A20_BIOS_UNAVAILABLE      2

start:
    cli
    xor ax, ax
    mov ss, ax
    mov sp, 0x7C00
    ; SeaBIOS uses 32-bit stack addressing internally (ESP). Make sure the high
    ; half of ESP is known-zero in real mode, otherwise INT 15h (E820) may fail.
    mov esp, 0x00007C00
    mov ax, cs
    mov ds, ax
    mov es, ax
    sti

    mov [boot_drive], dl
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_newline
    call debug_print_str
    mov si, msg_s2_start
    call debug_print_str
    mov al, [boot_drive]
    call debug_print_hex8
    mov si, msg_newline
    call debug_print_str
%endif

    call ensure_a20

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

    ; Collect BIOS memory map early, before loading big payloads into low memory.
    call collect_e820

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

    call populate_stage3_params

    ; --- Probe current BIOS video mode (INT 10h AH=0x0F) and locate a usable VBE LFB mode
    ; Clear bootinfo scratch area first to avoid stale data
    push ax
    push cx
    push di
    push es
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    mov cx, BOOTINFO_CLEAR_WORDS
    xor ax, ax
    rep stosw
    pop es
    pop di
    pop cx
    pop ax

    ; Record BIOS memory sizing fallbacks into the bootinfo scratch area.
    ; Stage3 snapshots these fields before rebuilding the C boot_info_t struct.
    call record_bios_meminfo

    push ax
    push bx
    push cx
    push dx
    push bp
    ; record current BIOS mode as fallback
    mov ah, 0x0F
    int 0x10
    movzx eax, al
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_MODE
    mov [es:di], al
    mov [es:di+1], ah

    call vbe_probe_and_record
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax

    call preload_kernel_if_needed

    ; --- Serial Boot Prompt ---
    mov si, msg_serial_prompt
    call debug_print_str
    
    ; Wait ~2 seconds for 's' or 'S'
    mov cx, 20          ; 20 * 100ms
.wait_s:
    mov ah, 0x01
    int 0x16            ; Check for key
    jnz .check_key
    
    ; Delay ~100ms
    push cx
    mov ax, 0x8600
    mov cx, 0x0001
    mov dx, 0x86A0      ; 100,000 microseconds
    int 0x15
    pop cx
    loop .wait_s
    jmp .no_serial

.check_key:
    mov ah, 0x00
    int 0x16            ; Get key
    or al, 0x20         ; Lowercase
    cmp al, 's'
    jne .wait_s
    
    ; Serial boot selected
    mov si, msg_serial_sel
    call debug_print_str
    ; Set FLAG_SERIAL_BOOT (defined in boot_shared.inc)
    or dword [stage3_params + STAGE3_PARAM_FLAGS], STAGE3_FLAG_SERIAL_BOOT
    jmp .finish_s2

.no_serial:
    mov si, msg_serial_none
    call debug_print_str

.finish_s2:

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
    mov al, 'F'
    call dbg_e9
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
    mov al, 'P'
    call dbg_e9
    or eax, 1
    mov cr0, eax
    mov esi, stage3_params
    add esi, STAGE2_LINEAR_ADDR
    mov edi, BOOTINFO_ADDR
far_jmp_label:
    mov al, 'J'
    call dbg_e9
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
; VBE probe (stay in text mode, record preferred LFB if available)
; -----------------------------------------------------------------------------
vbe_probe_and_record:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    ; reset selection state
    xor ax, ax
    mov [vesa_selected_mode], ax
    mov [vesa_selected_pitch], ax
    mov [vesa_selected_width], ax
    mov [vesa_selected_height], ax
    mov [vesa_selected_fb_lo], ax
    mov [vesa_selected_fb_hi], ax
    mov byte [vesa_selected_bpp], 0
    mov byte [vesa_match_level], 0

    mov ax, cs
    mov ds, ax
    mov es, ax
    lea di, [vbe_info_block]
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .done
    cmp dword [vbe_info_block], 0x41534556        ; 'VESA'
    jne .done

    ; store reported VRAM size (TotalMemory in 64 KiB blocks) → bytes
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    add di, BOOTINFO_VBE_MEM_BYTES
    mov ax, [vbe_info_block + 0x12]
    xor dx, dx
    mov dx, ax           ; low word zero, high word = blocks * 64 KiB
    xor ax, ax
    mov [es:di], ax
    mov [es:di + 2], dx

    ; iterate mode list
    mov bx, [cs:vbe_info_block + 0x0E]    ; mode list offset
    mov cx, [cs:vbe_info_block + 0x10]    ; mode list segment
    cmp bx, 0
    je .done
    mov ds, cx
    mov si, bx
.mode_loop:
    lodsw
    mov dx, ax           ; dx = mode
    cmp dx, 0xFFFF
    je .modes_done
    push si

    mov cx, dx           ; CX = mode for 4F01
    mov ax, cs
    mov es, ax
    lea di, [cs:vbe_mode_info]
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .mode_next

    mov ax, cs
    mov es, ax
    mov bx, [es:vbe_mode_info]         ; mode attributes
    test bx, 1                         ; supported
    jz .mode_next
    test bx, 0x0080                    ; linear framebuffer available
    jz .mode_next
%if VBE_ENABLE_LFB
%else
    jmp .mode_next
%endif
    mov al, [es:vbe_mode_info + 0x19]  ; bpp
    cmp al, 0
    je .mode_next
    mov bp, [es:vbe_mode_info + 0x10]  ; pitch (bytes/line)
    mov si, [es:vbe_mode_info + 0x12]  ; width
    mov di, [es:vbe_mode_info + 0x14]  ; height
    mov bx, [es:vbe_mode_info + 0x28]  ; fb base low
    mov cx, [es:vbe_mode_info + 0x2A]  ; fb base high
    cmp bx, 0
    jne .fb_ok
    cmp cx, 0
    je .mode_next
.fb_ok:
    xor ah, ah
    cmp al, VBE_PREF_BPP
    jne .chk_width
    add ah, 2
.chk_width:
    cmp si, VBE_PREF_WIDTH
    jne .chk_height
    inc ah
.chk_height:
    cmp di, VBE_PREF_HEIGHT
    jne .after_score
    inc ah
.after_score:
    cmp al, 8
    jne .score_ready
    inc ah
.score_ready:
    cmp ah, [cs:vesa_match_level]
    jbe .mode_next

    mov [cs:vesa_match_level], ah
    mov [cs:vesa_selected_mode], dx
    mov [cs:vesa_selected_pitch], bp
    mov [cs:vesa_selected_width], si
    mov [cs:vesa_selected_height], di
    mov [cs:vesa_selected_fb_lo], bx
    mov [cs:vesa_selected_fb_hi], cx
    mov [cs:vesa_selected_bpp], al

.mode_next:
    pop si
    jmp .mode_loop

.modes_done:
    ; write selected mode into bootinfo if any
    cmp byte [cs:vesa_match_level], 0
    je .done
    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET
    mov ax, [cs:vesa_selected_mode]
    mov [es:di + BOOTINFO_VBE_MODE], ax
    mov ax, [cs:vesa_selected_pitch]
    mov [es:di + BOOTINFO_VBE_PITCH], ax
    mov ax, [cs:vesa_selected_width]
    mov [es:di + BOOTINFO_VBE_WIDTH], ax
    mov ax, [cs:vesa_selected_height]
    mov [es:di + BOOTINFO_VBE_HEIGHT], ax
    mov al, [cs:vesa_selected_bpp]
    mov [es:di + BOOTINFO_VBE_BPP], al
    mov ax, [cs:vesa_selected_fb_lo]
    mov [es:di + BOOTINFO_FB_ADDR], ax
    mov ax, [cs:vesa_selected_fb_hi]
    mov [es:di + BOOTINFO_FB_ADDR + 2], ax

.done:
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; Helpers and BIOS interaction
; -----------------------------------------------------------------------------

; Record conventional + extended memory sizing info (fallback for broken/absent E820).
; Writes to bootinfo scratch (ES:BOOTINFO_OFFSET + BOOTINFO_BIOS_*).
record_bios_meminfo:
    push ax
    push bx
    push cx
    push dx
    push bp
    push si
    push di
    push ds
    push es

    mov ax, BOOTINFO_SEGMENT
    mov es, ax
    mov di, BOOTINFO_OFFSET

    ; flags=0
    mov word [es:di + BOOTINFO_BIOS_MEM_FLAGS], 0
    ; ext_kb=0 (dword)
    mov word [es:di + BOOTINFO_BIOS_EXT_KB], 0
    mov word [es:di + BOOTINFO_BIOS_EXT_KB + 2], 0

    ; Conventional memory in KiB (INT 12h)
    int 0x12
    mov [es:di + BOOTINFO_BIOS_CONV_KB], ax
    mov ax, [es:di + BOOTINFO_BIOS_MEM_FLAGS]
    or ax, 0x0001
    mov [es:di + BOOTINFO_BIOS_MEM_FLAGS], ax

    ; Try INT 15h AX=E801 (preferred)
    push ds
    xor ax, ax
    mov ds, ax
    mov ax, 0xE801
    int 0x15
    pop ds
    jc .try_88

    ; Some BIOSes return values in CX/DX instead of AX/BX.
    mov si, ax          ; below-16MiB KiB
    mov bp, bx          ; above-16MiB blocks (64KiB)
    cmp si, 0
    jne .have_e801
    cmp bp, 0
    jne .have_e801
    mov si, cx          ; use CX
    mov bp, dx          ; use DX
.have_e801:
    ; ext_kb = below16_kb + above16_blocks * 64
    mov ax, bp
    mov bx, 64
    mul bx              ; DX:AX = CX * 64
    add ax, si
    adc dx, 0
    mov [es:di + BOOTINFO_BIOS_EXT_KB], ax
    mov [es:di + BOOTINFO_BIOS_EXT_KB + 2], dx
    mov ax, [es:di + BOOTINFO_BIOS_MEM_FLAGS]
    or ax, 0x0002
    mov [es:di + BOOTINFO_BIOS_MEM_FLAGS], ax
    jmp .done

.try_88:
    ; INT 15h AH=88h: extended memory size in KiB above 1MiB (often capped at 64MiB)
    push ds
    xor ax, ax
    mov ds, ax
    mov ah, 0x88
    int 0x15
    pop ds
    jc .done
    xor dx, dx
    mov [es:di + BOOTINFO_BIOS_EXT_KB], ax
    mov [es:di + BOOTINFO_BIOS_EXT_KB + 2], dx
    mov ax, [es:di + BOOTINFO_BIOS_MEM_FLAGS]
    or ax, 0x0004
    mov [es:di + BOOTINFO_BIOS_MEM_FLAGS], ax

.done:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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

; Tiny helper: emit AL to Bochs/QEMU debug port 0xE9 without clobbering state
dbg_e9:
    push ax
    push dx
    mov dx, 0x00E9
    out dx, al
    pop dx
    pop ax
    ret

; Hex printing via dbg_e9 (0xE9), useful for early headless debugging.
; Expect nibble in AL.
dbg_e9_hex_digit:
    push ax
    push bx
    xor bh, bh
    mov bl, al
    and bl, 0x0F
    mov al, [hex_digits + bx]
    call dbg_e9
    pop bx
    pop ax
    ret

; Expect value in AL.
dbg_e9_hex8:
    push ax
    mov ah, al
    shr al, 4
    call dbg_e9_hex_digit
    mov al, ah
    and al, 0x0F
    call dbg_e9_hex_digit
    pop ax
    ret

; Expect value in AX.
dbg_e9_hex16:
    push ax
    mov al, ah
    call dbg_e9_hex8
    pop ax
    call dbg_e9_hex8
    ret

; Expect value in DX:AX (DX high word, AX low word).
dbg_e9_hex32:
    push ax
    push dx
    mov ax, dx
    call dbg_e9_hex16
    pop dx
    pop ax
    call dbg_e9_hex16
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
    push ds
    push es
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov di, e820_entries
    mov word [e820_entry_count], 0
%if STAGE2_E820_DEBUG
    mov al, 'E'
    call dbg_e9
%endif

    ; First try: request 24-byte entries (ACPI 3.0+)
    xor ebx, ebx
.loop24:
    cmp word [e820_entry_count], E820_MAX_ENTRIES
    jae .done24
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .dbg24_pre_done
    mov al, '4'
    call dbg_e9
    mov al, 'p'
    call dbg_e9
    mov ax, cs
    call dbg_e9_hex16
    mov al, ':'
    call dbg_e9
    mov ax, di
    call dbg_e9_hex16
    ; Dump IVT entry for INT 15h (0x15*4=0x54): seg:off
    mov al, 'v'
    call dbg_e9
    push bx
    push ds
    xor ax, ax
    mov ds, ax
    mov bx, [0x0054]
    mov ax, [0x0056]
    pop ds
    call dbg_e9_hex16
    mov al, ':'
    call dbg_e9
    mov ax, bx
    call dbg_e9_hex16
    pop bx
.dbg24_pre_done:
%endif
    mov eax, 0x0000E820
    mov edx, 0x534D4150
    mov ecx, 24
    ; ES:DI is the output buffer. Set ES without clobbering EAX.
    push cs
    pop es
    ; Some BIOSes require the ext-attr dword to be nonzero when requesting 24 bytes.
    mov dword [es:di + 20], 1
    movzx edi, di       ; Some BIOSes read EDI, not just DI
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .dbg24_in_done
    mov al, 'i'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'D'
    call dbg_e9
    mov [tmp_dword], edx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'B'
    call dbg_e9
    mov [tmp_dword], ebx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'L'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32

    ; Reload volatile inputs after debug printing (loads clobber AX/DX).
    mov eax, 0x0000E820
    mov edx, 0x534D4150
    mov ecx, 24
    push cs
    pop es
    mov dword [es:di + 20], 1
    movzx edi, di
.dbg24_in_done:
%endif
    push di
    push ds
    xor si, si          ; Some BIOS routines expect DS=0 (BDA).
    mov ds, si
    int 0x15
    pop ds
    pop di
    ; BIOS calls may clobber ES (and sometimes DS). Restore without touching EAX.
    push cs
    pop ds
    push cs
    pop es
    jc .fail24_cf
    cmp eax, 0x534D4150
    jne .fail24_sig
    cmp ecx, 20
    jb .fail24_len
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .dbg24_post_done
    mov al, 'c'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'B'
    call dbg_e9
    mov [tmp_dword], ebx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'L'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
.dbg24_post_done:
%endif
    ; If BIOS returned only 20 bytes, clear attr for this entry.
    cmp ecx, 24
    jae .have_attr24
    mov dword [es:di + 20], 0
.have_attr24:
    inc word [e820_entry_count]
    add di, 24
    test ebx, ebx
    jne .loop24
    jmp .done24
.fail24_cf:
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .done24
    mov al, 'C'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'B'
    call dbg_e9
    mov [tmp_dword], ebx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'L'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
%endif
    jmp .done24
.fail24_sig:
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .done24
    mov al, 'S'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
%endif
    jmp .done24
.fail24_len:
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .done24
    mov al, 'L'
    call dbg_e9
    mov al, 'l'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
%endif
    jmp .done24
.done24:
    cmp word [e820_entry_count], 0
    jne .done

    ; Fallback: retry with 20-byte entries (older BIOSes).
    mov di, e820_entries
    mov word [e820_entry_count], 0
    xor ebx, ebx
.loop20:
    cmp word [e820_entry_count], E820_MAX_ENTRIES
    jae .done
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .dbg20_pre_done
    mov al, '2'
    call dbg_e9
    mov al, 'p'
    call dbg_e9
    mov ax, cs
    call dbg_e9_hex16
    mov al, ':'
    call dbg_e9
    mov ax, di
    call dbg_e9_hex16
.dbg20_pre_done:
%endif
    mov eax, 0x0000E820
    mov edx, 0x534D4150
    mov ecx, 20
    push cs
    pop es
    ; Keep our in-memory format 24 bytes/entry; attr=0 for 20-byte returns.
    mov dword [es:di + 20], 0
    movzx edi, di
    push di
    push ds
    xor si, si
    mov ds, si
    int 0x15
    pop ds
    pop di
    push cs
    pop ds
    push cs
    pop es
    jc .fail20_cf
    cmp eax, 0x534D4150
    jne .fail20_sig
    cmp ecx, 20
    jb .fail20_len
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .dbg20_post_done
    mov al, 'c'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'B'
    call dbg_e9
    mov [tmp_dword], ebx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'L'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
.dbg20_post_done:
%endif
    inc word [e820_entry_count]
    add di, 24
    test ebx, ebx
    jne .loop20
    jmp .done
.fail20_cf:
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .done
    mov al, 'C'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'B'
    call dbg_e9
    mov [tmp_dword], ebx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
    mov al, 'L'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
%endif
    jmp .done
.fail20_sig:
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .done
    mov al, 'S'
    call dbg_e9
    mov al, 'A'
    call dbg_e9
    mov [tmp_dword], eax
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
%endif
    jmp .done
.fail20_len:
%if STAGE2_E820_DEBUG
    cmp word [e820_entry_count], 0
    jne .done
    mov al, 'L'
    call dbg_e9
    mov al, 'l'
    call dbg_e9
    mov [tmp_dword], ecx
    mov ax, [tmp_dword]
    mov dx, [tmp_dword + 2]
    call dbg_e9_hex32
%endif
    jmp .done

.done:
%if STAGE2_E820_DEBUG
    mov al, 'n'
    call dbg_e9
    mov ax, [e820_entry_count]
    call dbg_e9_hex16
%endif
    pop es
    pop ds
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
    cmp byte [a20_state], A20_STATE_ENABLED
    jne .flags_store
    or eax, STAGE3_FLAG_A20_ENABLED
.flags_store:
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

; Ensure A20 gate enabled via Fast A20, INT 15h, then KBC fallback
ensure_a20:
    push cx
    push dx
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_newline
    call debug_print_str
    mov si, msg_a20_probe
    call debug_print_str
%endif
    mov al, [a20_state]
    cmp al, A20_STATE_UNKNOWN
    jne .report

    call test_a20_wrap
    test ax, ax
    jnz .already_enabled
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_newline
    call debug_print_str
    mov si, msg_a20_need
    call debug_print_str
%endif

.attempt_enable:
    mov cx, 3
.fast_retry:
%if STAGE2_VERBOSE_DEBUG
    mov al, 'f'
    call debug_char
%endif
    call enable_a20_fast
    call test_a20_wrap
    test ax, ax
    jnz .success
    loop .fast_retry

    cmp byte [a20_bios_capability], A20_BIOS_UNKNOWN
    jne .skip_probe
    call probe_a20_bios_capability
.skip_probe:
    cmp byte [a20_bios_capability], A20_BIOS_SUPPORTED
    jne .after_bios
%if STAGE2_VERBOSE_DEBUG
    mov al, 'b'
    call debug_char
%endif
    call enable_a20_bios_int15
    call test_a20_wrap
    test ax, ax
    jnz .success
.after_bios:

%if ENABLE_A20_KBC
    mov cx, 5
.kbc_retry:
%if STAGE2_VERBOSE_DEBUG
    mov al, 'k'
    call debug_char
%endif
    call enable_a20_kbc
    call test_a20_wrap
    test ax, ax
    jnz .success
    loop .kbc_retry
%endif

    mov byte [a20_state], A20_STATE_UNAVAILABLE
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_newline
    call debug_print_str
    mov si, msg_a20_fail
    call debug_print_str
%endif
    jmp .report

.already_enabled:
%if STAGE2_VERBOSE_DEBUG
    mov si, msg_newline
    call debug_print_str
    mov si, msg_a20_already
    call debug_print_str
%endif
    jmp .success

.success:
    mov byte [a20_state], A20_STATE_ENABLED

.report:
%if STAGE2_VERBOSE_DEBUG
    mov al, [a20_state]
    cmp al, A20_STATE_ENABLED
    jne .dbg_off
    mov al, 1
    call report_a20
    jmp .dbg_done
.dbg_off:
    xor al, al
    call report_a20
.dbg_done:
%endif
    pop dx
    pop cx
    mov al, [a20_state]
    cmp al, A20_STATE_ENABLED
    jne .ret_zero
    mov al, 1
    ret
.ret_zero:
    xor al, al
    ret

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

probe_a20_bios_capability:
    push ax
    push bx
    push cx
    push dx
    mov ax, 0x2402
    int 0x15
    jc .no_support
    cmp ah, 0
    jne .no_support
    mov byte [a20_bios_capability], A20_BIOS_SUPPORTED
    jmp .done
.no_support:
    mov byte [a20_bios_capability], A20_BIOS_UNAVAILABLE
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

enable_a20_bios_int15:
    push ax
    push bx
    push cx
    push dx
    cmp byte [a20_bios_capability], A20_BIOS_SUPPORTED
    jne .done
    mov ax, 0x2403
    int 0x15
    jc .done
    cmp ah, 0
    jne .done
    test al, al
    jz .done
    mov ax, 0x2401
    int 0x15
.done:
    pop dx
    pop cx
    pop bx
    pop ax
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
msg_serial_prompt:   db 13,10,'Press [S] for Serial Loader (2s)...',0
msg_serial_sel:      db ' [SERIAL MODE]',13,10,0
msg_serial_none:     db ' [FLOPPY MODE]',13,10,0
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
msg_a20_probe:       db 'A20?',0
msg_a20_need:        db 'A20 enabling',0
msg_a20_already:     db 'A20 already enabled',0
msg_a20_fail:        db 'A20 enable failed',0
msg_err_chs:         db 'CHS!',0
msg_err_lba:         db 'LBA!',0
msg_sel_code:        db 'SELc=0x',0
msg_sel_data:        db ' SELd=0x',0
msg_far_bytes:       db 'JMP=',0
msg_far_sel:         db ' sel=0x',0
msg_far_off:         db ' off=0x',0
msg_far_lin:         db ' lin=0x',0
msg_newline:         db 0x0D,0x0A,0

; Buffers for VBE controller/mode info
align 4
vbe_info_block:  times 512 db 0
align 4
vbe_mode_info:   times 256 db 0

; VBE selection state
align 2
vesa_selected_mode:   dw 0
vesa_selected_pitch:  dw 0
vesa_selected_width:  dw 0
vesa_selected_height: dw 0
vesa_selected_fb_lo:  dw 0
vesa_selected_fb_hi:  dw 0
vesa_selected_bpp:    db 0
vesa_match_level:     db 0

; VBE bootlog messages removed (kernel will log bootinfo)

stage3_signature:    db 0xB8,0x21,0x47,0x53,0x33,0x31,0xC0

boot_drive:           db 0
a20_state:            db A20_STATE_UNKNOWN
a20_bios_capability:  db A20_BIOS_UNKNOWN
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
align 16
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
    mov dx, 0x00E9
    mov al, 'S'
    out dx, al
    jmp 0x08:STAGE3_LINEAR_ADDR
[BITS 16]
