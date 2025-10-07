; boot_config.inc - Common boot stage configuration (overridable at build time)
%ifndef BOOT_CONFIG_INC
%define BOOT_CONFIG_INC

; Disk layout
%ifndef STAGE2_START_SECTOR
%define STAGE2_START_SECTOR 2       ; First sector after the boot sector
%endif

%ifndef STAGE2_SECTORS
%define STAGE2_SECTORS 4            ; Default Stage 2 span (can be overridden by make)
%endif

%ifndef STAGE3_START_SECTOR
%define STAGE3_START_SECTOR (STAGE2_START_SECTOR + STAGE2_SECTORS)
%endif

; Memory layout
%ifndef STAGE2_LOAD_SEGMENT
%define STAGE2_LOAD_SEGMENT 0x1000  ; 0x10000 physical address for Stage 2
%endif

%ifndef STAGE2_LINEAR_ADDR
%define STAGE2_LINEAR_ADDR (STAGE2_LOAD_SEGMENT << 4)
%endif

%ifndef STAGE3_LOAD_SEGMENT
%define STAGE3_LOAD_SEGMENT 0x2000  ; 0x20000 physical address for Stage 3 image
%endif

%ifndef STAGE3_LINEAR_ADDR
%define STAGE3_LINEAR_ADDR (STAGE3_LOAD_SEGMENT << 4)
%endif

%ifndef STAGE3_STACK_TOP
%define STAGE3_STACK_TOP 0x0009F000 ; temporary 32-bit stack top (below 640K barrier)
%endif

%ifndef STAGE1_VERBOSE_DEBUG
%define STAGE1_VERBOSE_DEBUG 1
%endif

%ifndef STAGE2_VERBOSE_DEBUG
%define STAGE2_VERBOSE_DEBUG 1
%endif

%ifndef STAGE3_VERBOSE_DEBUG
%define STAGE3_VERBOSE_DEBUG 1
%endif

%ifndef ENABLE_STAGE1_DEBUG
%define ENABLE_STAGE1_DEBUG STAGE1_VERBOSE_DEBUG
%endif

%ifndef KERNEL_START_SECTOR
%define KERNEL_START_SECTOR (STAGE3_START_SECTOR + STAGE3_SECTORS)
%endif

%ifndef KERNEL_SECTORS
%define KERNEL_SECTORS 0
%endif

%ifndef KERNEL_LOAD_LINEAR
%define KERNEL_LOAD_LINEAR KERNEL_LOAD_ADDR
%endif

; Stage 1 options
%ifndef ENABLE_STAGE1_A20
%define ENABLE_STAGE1_A20 0
%endif

%endif ; BOOT_CONFIG_INC
