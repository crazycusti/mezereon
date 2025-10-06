# Compiler-Auswahl: Standard gcc (host)
CC ?= gcc
AS = nasm
# Linker und Objcopy: host tools
LD ?= ld
# Robust objcopy detection (host first)
OBJCOPY := $(shell command -v objcopy 2>/dev/null || command -v gobjcopy 2>/dev/null || echo objcopy)
CFLAGS ?= -ffreestanding -m32 -march=i386 -mtune=i386 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow -mno-avx -fno-if-conversion -fno-if-conversion2 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie
LDFLAGS ?= -Ttext 0x8000 -m elf_i386

STAGE2_START_SECTOR := 2
STAGE3_LINK_ADDR    := 0x00020000
KERNEL_LOAD_LINEAR  := 0x00008000
STAGE2_FORCE_CHS    ?= 0

# Optional QEMU acceleration: set QEMU_ACCEL=kvm|hvf|whpx to enable hardware acceleration and host CPU model
ifeq ($(QEMU_ACCEL),kvm)
QEMU_ACCEL_FLAGS := -enable-kvm -cpu host
endif

# Optional: host port forwarding to guest (usernet)
# Set HTTP_HOST_PORT to forward host port to guest TCP/80 (web server)
# Example: HTTP_HOST_PORT=8080 make run-x86-hdd-ne2k → curl http://127.0.0.1:8080
HTTP_HOST_PORT ?=
QEMU_USER_NETDEV := -netdev user,id=n0
ifneq ($(HTTP_HOST_PORT),)
QEMU_USER_NETDEV := -netdev user,id=n0,hostfwd=tcp::$(HTTP_HOST_PORT)-:80
endif
ifeq ($(QEMU_ACCEL),hvf)
QEMU_ACCEL_FLAGS := -accel hvf -cpu host
endif
ifeq ($(QEMU_ACCEL),whpx)
QEMU_ACCEL_FLAGS := -accel whpx -cpu host
endif

## Using host gcc/ld/objcopy; ensure 32-bit support libraries are installed for -m32.

CONFIG_NE2000_IO ?= 0x300
CONFIG_NE2000_IRQ ?= 3
CONFIG_NE2000_IO_SIZE ?= 32

CDEFS = -DCONFIG_NE2000_IO=$(CONFIG_NE2000_IO) -DCONFIG_NE2000_IRQ=$(CONFIG_NE2000_IRQ) -DCONFIG_NE2000_IO_SIZE=$(CONFIG_NE2000_IO_SIZE)

# Console backend selection (default: vga)
CONSOLE_BACKEND ?= vga
ifeq ($(CONSOLE_BACKEND),vga)
CONSOLE_BACKEND_OBJ = console_backend_vga.o
else
CONSOLE_BACKEND_OBJ = console_backend_fb_stub.o
endif



all: disk.img

BOOTINFO ?= 1
DEBUG_BOOT ?= 0
A20_KBC ?= $(DEBUG_BOOT)
WAIT_PM ?= 0
DEBUG_PM_STUB ?= 0
ENTRY32_DEBUG ?= 0

stage1.bin: stage1.asm stage2.bin
	s2s=$$(expr \( $$(wc -c < stage2.bin) + 511 \) / 512); \
	$(AS) -f bin -D STAGE2_SECTORS=$$s2s $< -o $@

stage2.bin: stage2.asm stage3.bin kernel_payload.bin version.h
	s3s=$$(expr \( $$(wc -c < stage3.bin) + 511 \) / 512); \
	ks=$$(expr \( $$(wc -c < kernel_payload.bin) + 511 \) / 512); \
	s3start_guess=$$(expr $(STAGE2_START_SECTOR) + 1); \
	kstart_guess=$$(expr $$s3start_guess + $$s3s); \
	$(AS) -f bin -D STAGE2_SECTORS=1 -D STAGE3_SECTORS=$$s3s -D STAGE3_START_SECTOR=$$s3start_guess -D KERNEL_SECTORS=$$ks -D KERNEL_START_SECTOR=$$kstart_guess -D KERNEL_LOAD_LINEAR=$(KERNEL_LOAD_LINEAR) -D STAGE2_FORCE_CHS=$(STAGE2_FORCE_CHS) -D ENABLE_BOOTINFO=$(BOOTINFO) -D DEBUG_BOOT=$(DEBUG_BOOT) -D ENABLE_A20_KBC=$(A20_KBC) -D WAIT_BEFORE_PM=$(WAIT_PM) -D DEBUG_PM_STUB=$(DEBUG_PM_STUB) $< -o $@; \
	s2s=$$(expr \( $$(wc -c < $@) + 511 \) / 512); \
	s3start=$$(expr $(STAGE2_START_SECTOR) + $$s2s); \
	kstart=$$(expr $$s3start + $$s3s); \
	$(AS) -f bin -D STAGE2_SECTORS=$$s2s -D STAGE3_SECTORS=$$s3s -D STAGE3_START_SECTOR=$$s3start -D KERNEL_SECTORS=$$ks -D KERNEL_START_SECTOR=$$kstart -D KERNEL_LOAD_LINEAR=$(KERNEL_LOAD_LINEAR) -D STAGE2_FORCE_CHS=$(STAGE2_FORCE_CHS) -D ENABLE_BOOTINFO=$(BOOTINFO) -D DEBUG_BOOT=$(DEBUG_BOOT) -D ENABLE_A20_KBC=$(A20_KBC) -D WAIT_BEFORE_PM=$(WAIT_PM) -D DEBUG_PM_STUB=$(DEBUG_PM_STUB) $< -o $@

stage3_entry.o: stage3_entry.asm boot_config.inc
	$(AS) -f elf32 $< -o $@

stage3_main.o: stage3.c stage3_params.h bootinfo.h
	$(CC) $(CFLAGS) -c $< -o $@

stage3.elf: stage3_entry.o stage3_main.o
	$(LD) -m elf_i386 -Ttext $(STAGE3_LINK_ADDR) -e stage3_entry $^ -o $@

stage3.bin: stage3.elf
	$(OBJCOPY) -O binary $< $@
	truncate -s %512 $@

bootloader.bin: stage1.bin stage2.bin stage3.bin
	cat stage1.bin stage2.bin stage3.bin > $@

entry32.o: entry32.asm
	$(AS) -f elf32 -D DEBUG_ENTRY32=$(ENTRY32_DEBUG) -D ENABLE_BOOTINFO=$(BOOTINFO) $< -o $@

isr.o: isr.asm
	$(AS) -f elf32 $< -o $@

main.o: main.c main.h config.h display.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

memory.o: memory.c memory.h bootinfo.h console.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

video.o: video.c main.h config.h display.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

console.o: console.c console.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

console_backend_vga.o: console_backend_vga.c console_backend.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

console_backend_fb_stub.o: console_backend_fb_stub.c console_backend.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

netface.o: netface.c netface.h config.h drivers/ne2000.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

net/ipv4.o: net/ipv4.c net/ipv4.h netface.h console.h platform.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

net/tcp_min.o: net/tcp_min.c net/tcp_min.h net/ipv4.h console.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

mezapi.o: mezapi.c mezapi.h console.h keyboard.h platform.h drivers/pcspeaker.h drivers/sb16.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

apps/keymusic_app.o: apps/keymusic_app.c ./mezapi.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

apps/rotcube_app.o: apps/rotcube_app.c ./mezapi.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

apps/fbtest_color.o: apps/fbtest_color.c apps/fbtest_color.h display.h console.h drivers/gpu/gpu.h keyboard.h cpuidle.h netface.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

fonts/font8x16.o: fonts/font8x16.c fonts/font8x16.h fonts/font8x16.inc
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

platform.o: platform.c platform.h interrupts.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@
drivers/ne2000.o: drivers/ne2000.c drivers/ne2000.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@
drivers/pcspeaker.o: drivers/pcspeaker.c drivers/pcspeaker.h arch/x86/io.h platform.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/sb16.o: drivers/sb16.c drivers/sb16.h config.h arch/x86/io.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/pci.o: drivers/pci.c drivers/pci.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/gpu/gpu.o: drivers/gpu/gpu.c drivers/gpu/gpu.h drivers/gpu/cirrus.h drivers/pci.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/gpu/cirrus.o: drivers/gpu/cirrus.c drivers/gpu/cirrus.h drivers/gpu/gpu.h drivers/pci.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/gpu/cirrus_accel.o: drivers/gpu/cirrus_accel.c drivers/gpu/cirrus_accel.h drivers/gpu/fb_accel.h drivers/gpu/vga_hw.h display.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/gpu/fb_accel.o: drivers/gpu/fb_accel.c drivers/gpu/fb_accel.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/gpu/et4000.o: drivers/gpu/et4000.c drivers/gpu/et4000.h drivers/gpu/gpu.h drivers/gpu/vga_hw.h drivers/gpu/fb_accel.h config.h display.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/gpu/vga_hw.o: drivers/gpu/vga_hw.c drivers/gpu/vga_hw.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/ata.o: drivers/ata.c drivers/ata.h config.h main.h keyboard.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/fs/neelefs.o: drivers/fs/neelefs.c drivers/fs/neelefs.h drivers/ata.h config.h main.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/storage.o: drivers/storage.c drivers/storage.h drivers/ata.h drivers/fs/neelefs.h console.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

keyboard.o: keyboard.c keyboard.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

shell.o: shell.c shell.h keyboard.h config.h main.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

cpu.o: cpu.c cpu.h console.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

cpuidle.o: cpuidle.c cpuidle.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

kernel_payload.elf: entry32.o kentry.o isr.o idt.o interrupts.o platform.o main.o memory.o video.o console.o display.o fonts/font8x16.o $(CONSOLE_BACKEND_OBJ) netface.o net/ipv4.o net/tcp_min.o mezapi.o apps/keymusic_app.o apps/rotcube_app.o apps/fbtest_color.o drivers/ne2000.o drivers/pcspeaker.o drivers/sb16.o drivers/pci.o drivers/gpu/gpu.o drivers/gpu/cirrus.o drivers/gpu/cirrus_accel.o drivers/gpu/et4000.o drivers/gpu/fb_accel.o drivers/gpu/vga_hw.o drivers/ata.o drivers/fs/neelefs.o drivers/storage.o keyboard.o cpu.o cpuidle.o shell.o
	$(LD) $(LDFLAGS) $^ -o $@

# Erzeuge flaches Binary ohne führende 0x8000-Lücke
kernel_payload.bin: kernel_payload.elf
	$(OBJCOPY) -O binary --change-addresses -0x8000 $< $@
	truncate -s %512 $@

disk.img: bootloader.bin kernel_payload.bin
	cat $^ > $@

version.h:
	@printf '#define GIT_REV "%s"\n' "$(shell git describe --always --dirty 2>/dev/null || git rev-parse --short HEAD)" > $@

main.o: version.h

## --- QEMU run helpers (x86) ---
.PHONY: run-x86-floppy run-x86-hdd
run-x86-floppy: disk.img
	$(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		$(QEMU_ACCEL_FLAGS) -drive file=disk.img,if=floppy,format=raw \
		-net none -display curses

run-x86-hdd: disk.img
	$(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		$(QEMU_ACCEL_FLAGS) -drive file=disk.img,format=raw,if=ide \
		-net none -display curses

.PHONY: run-x86-hdd-ne2k
run-x86-hdd-ne2k: disk.img
	$(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		$(QEMU_ACCEL_FLAGS) -drive file=disk.img,format=raw,if=ide \
		-device ne2k_isa,netdev=n0,iobase=$(CONFIG_NE2000_IO),irq=$(CONFIG_NE2000_IRQ) \
		$(QEMU_USER_NETDEV) -display curses

# Headless smoke test (CI/VS Code): no curses/X required; runs for a few seconds
.PHONY: test-x86-ne2k
test-x86-ne2k: disk.img
	@echo "[TEST] Starting headless QEMU (NE2000 @ $(CONFIG_NE2000_IO), IRQ $(CONFIG_NE2000_IRQ)) for 6s..."
	@timeout 6 $(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		-drive file=disk.img,format=raw,if=ide \
		-device ne2k_isa,netdev=n0,iobase=$(CONFIG_NE2000_IO),irq=$(CONFIG_NE2000_IRQ) \
		-netdev user,id=n0 -display none -monitor none -serial none || true
	@echo "[TEST] Done (timeout or normal exit)."

.PHONY: test
test: test-x86-ne2k


# --- Help target ---
.PHONY: help
help:
	@echo "Mezereon — common make targets"
	@echo ""
	@echo "x86:"
	@echo "  make                  Build x86 floppy image (disk.img)"
	@echo "  make run-x86-floppy   Run QEMU (curses terminal) with disk.img as floppy"
	@echo "  make run-x86-hdd      Run QEMU (curses terminal) with disk.img as IDE disk (no NIC)"
	@echo "  make run-x86-hdd-ne2k Run QEMU (curses terminal) IDE + NE2000 ISA (usernet)"
	@echo "  make test-x86-ne2k    Headless smoke test (6s, no TTY required)"
	@echo ""
	@echo "SPARC (OpenBIOS/SS-5):"
	@echo "  make sparc-boot       Build SPARC client (boot.elf/aout/bin)"
	@echo "  make run-sparc-tftp   Netboot a.out via QEMU/OpenBIOS TFTP"
	@echo "  make run-sparc-kernel Direct -kernel boot of boot.elf"
	@echo "  make run-sparc-cdrom  Boot SPARC ISO (client program)"
	@echo ""
	@echo "Maintenance:"
	@echo "  make clean            Remove build artifacts"
	@echo "  make distclean        Clean + prune untracked build outputs"
	@echo "  make log MSG=\"...\"  Append short entry to CHANGELOG"
	@echo ""
	@echo "Config (variables):"
	@echo "  CONSOLE_BACKEND=vga|fb (default vga)"
	@echo "  CONFIG_NE2000_IO=0x300 CONFIG_NE2000_IRQ=3"
	@echo "  SPARC_CC (cross GCC), SPARC_OBJCOPY (cross objcopy)"
	@echo ""
	@echo "See README.md for details."


clean:
	# Root objs and binaries
	rm -f *.o *.bin *.img netface.o console.o $(CONSOLE_BACKEND_OBJ) video.o main.o entry32.o isr.o idt.o interrupts.o kentry.o kernel_payload.bin kernel_payload.elf bootloader.bin stage3.elf
	# Driver objects
	rm -f drivers/*.o drivers/*/*.o
	# SPARC artifacts
	rm -f arch/sparc/*.o arch/sparc/boot.elf arch/sparc/boot.aout arch/sparc/boot.bin arch/sparc/boot.iso
	rm -rf arch/sparc/cdroot
	# Symlinks and helper outputs
	rm -f boot.elf
	# TFTP payloads
	rm -rf tftp
	# Misc images and logs
	rm -f sun_boot_disk.img sparc_boot*.img sparc_*_elf_boot.img sparc_working_disk.img qemu.log

.PHONY: distclean
distclean: clean
	@echo "[CLEAN] Removing untracked build products (except VCS/metadata)"
	@git clean -fdx -e .git -e .gitignore -e .gitattributes -e license -e README.txt -e README.md -e CHANGELOG || true

# --- Simple changelog appender: make log MSG="changed X"
.PHONY: log
log:
	@tools/changelog.sh "$(MSG)"

# Optional: build a SPARC32 OBP client stub (cross or native SPARC host)
# Detect native SPARC host first, else try common cross toolchains and user-local builds.
SPARC_IS_NATIVE := $(shell uname -m | tr A-Z a-z | grep -c sparc || true)
ifeq ($(SPARC_IS_NATIVE),0)
  # Non-SPARC host: prefer cross toolchains; also scan crosstool-NG default prefixes
  # Optional explicit prefix: SPARC_PREFIX (e.g., sparc-custom-elf-) or CROSS_COMPILE
  ifneq ($(strip $(SPARC_PREFIX)$(CROSS_COMPILE)),)
    SPARC_CC_PREFIX_CANDIDATE := $(shell \
      command -v $(SPARC_PREFIX)gcc 2>/dev/null || \
      command -v $(CROSS_COMPILE)gcc 2>/dev/null || \
      echo "")
    SPARC_OBJCOPY_PREFIX_CANDIDATE := $(shell \
      command -v $(SPARC_PREFIX)objcopy 2>/dev/null || \
      command -v $(CROSS_COMPILE)objcopy 2>/dev/null || \
      echo "")
    ifneq ($(strip $(SPARC_CC_PREFIX_CANDIDATE)),)
      SPARC_CC ?= $(SPARC_CC_PREFIX_CANDIDATE)
    endif
    ifneq ($(strip $(SPARC_OBJCOPY_PREFIX_CANDIDATE)),)
      SPARC_OBJCOPY ?= $(SPARC_OBJCOPY_PREFIX_CANDIDATE)
    endif
  endif
  # Optional: honor TOOLCHAIN_DIR when provided (expects <dir>/bin/* in typical layouts)
  ifneq ($(strip $(TOOLCHAIN_DIR)),)
    SPARC_CC ?= $(shell \
      ls -1 $(TOOLCHAIN_DIR)/bin/sparc-unknown-elf-gcc 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/bin/sparc-elf-gcc 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/bin/sparc-*-elf-gcc 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/sparc-unknown-elf-gcc 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/sparc-elf-gcc 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/sparc-*-elf-gcc 2>/dev/null | head -n1 )
    SPARC_OBJCOPY ?= $(shell \
      ls -1 $(TOOLCHAIN_DIR)/bin/sparc-unknown-elf-objcopy 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/bin/sparc-elf-objcopy 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/bin/sparc-*-elf-objcopy 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/sparc-unknown-elf-objcopy 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/sparc-elf-objcopy 2>/dev/null | head -n1 || \
      ls -1 $(TOOLCHAIN_DIR)/sparc-*-elf-objcopy 2>/dev/null | head -n1 )
  endif
  SPARC_CC ?= $(shell \
    command -v sparc-elf-gcc 2>/dev/null || \
    command -v sparc-unknown-elf-gcc 2>/dev/null || \
    ls -1 $(HOME)/x-tools/*/bin/sparc-unknown-elf-gcc 2>/dev/null | head -n1 || \
    ls -1 /opt/x-tools/*/bin/sparc-unknown-elf-gcc 2>/dev/null | head -n1 || \
    ls -1 /usr/local/x-tools/*/bin/sparc-unknown-elf-gcc 2>/dev/null | head -n1 || \
    ls -1 $(HOME)/x-tools/*/bin/sparc-elf-gcc 2>/dev/null | head -n1 || \
    ls -1 /opt/x-tools/*/bin/sparc-elf-gcc 2>/dev/null | head -n1 || \
    ls -1 /usr/local/x-tools/*/bin/sparc-elf-gcc 2>/dev/null | head -n1 || \
    ls -1 $(HOME)/sparc32gcc/.build/*/*/bin/sparc-unknown-elf-gcc 2>/dev/null | head -n1 || \
    (IFS=:; for d in $$PATH; do \
       for n in sparc-*-elf-gcc sparc-unknown-*-gcc; do \
         if [ -x "$$d/$$n" ]; then echo "$$d/$$n"; exit 0; fi; \
       done; \
     done; echo sparc-elf-gcc))
  SPARC_OBJCOPY ?= $(shell \
    command -v sparc-elf-objcopy 2>/dev/null || \
    command -v sparc-unknown-elf-objcopy 2>/dev/null || \
    ls -1 $(HOME)/x-tools/*/bin/sparc-unknown-elf-objcopy 2>/dev/null | head -n1 || \
    ls -1 /opt/x-tools/*/bin/sparc-unknown-elf-objcopy 2>/dev/null | head -n1 || \
    ls -1 /usr/local/x-tools/*/bin/sparc-unknown-elf-objcopy 2>/dev/null | head -n1 || \
    ls -1 $(HOME)/x-tools/*/bin/sparc-elf-objcopy 2>/dev/null | head -n1 || \
    ls -1 /opt/x-tools/*/bin/sparc-elf-objcopy 2>/dev/null | head -n1 || \
    ls -1 /usr/local/x-tools/*/bin/sparc-elf-objcopy 2>/dev/null | head -n1 || \
    (IFS=:; for d in $$PATH; do \
       for n in sparc-*-elf-objcopy sparc-unknown-*-objcopy; do \
         if [ -x "$$d/$$n" ]; then echo "$$d/$$n"; exit 0; fi; \
       done; \
     done; echo objcopy))
  # If using host objcopy as fallback, try to derive a cross-objcopy next to SPARC_CC
  ifeq ($(SPARC_OBJCOPY),objcopy)
    SPARC_OBJCOPY := $(dir $(SPARC_CC))$(patsubst %gcc,%objcopy,$(notdir $(SPARC_CC)))
  endif
else
  # Native SPARC host: use system gcc/objcopy
  SPARC_CC ?= $(shell command -v gcc 2>/dev/null || echo gcc)
  SPARC_OBJCOPY ?= $(shell command -v objcopy 2>/dev/null || echo objcopy)
endif
SPARC_CFLAGS ?= -ffreestanding -nostdlib -Wall -Wextra -Os -mcpu=v8 -fno-pic -fno-pie -fno-builtin -mno-fpu -mflat -Wa,-Av8
SPARC_CDEFS ?= -DCONFIG_ARCH_X86=0 -DCONFIG_ARCH_SPARC=1
# Avoid external linker script; set entry + text base explicitly for -kernel and OF
SPARC_LDFLAGS ?= -ffreestanding -nostdlib -Wl,-Tarch/sparc/link.ld,-z,max-page-size=0x1000,-z,common-page-size=0x1000

.PHONY: sparc-boot check-sparc-toolchain
# Build SPARC client program variants (ELF for -kernel, a.out for OF, bin for manual)
sparc-boot: check-sparc-toolchain arch/sparc/boot.elf arch/sparc/boot.aout arch/sparc/boot.bin
	ln -sf arch/sparc/boot.elf boot.elf

# Toolchain sanity: fail clearly if the compiler is not available; warn about objcopy.
check-sparc-toolchain:
	@command -v $(SPARC_CC) >/dev/null 2>&1 || (echo "[SPARC] Compiler '$(SPARC_CC)' not found. Set SPARC_CC=/path/to/cross-gcc" && exit 1)
	@command -v $(SPARC_OBJCOPY) >/dev/null 2>&1 || echo "[SPARC] objcopy '$(SPARC_OBJCOPY)' not found; a.out/bin conversion may fall back to host objcopy if available."
	@echo "[SPARC] CC      = $(SPARC_CC)"
	@echo "[SPARC] OBJCOPY = $(SPARC_OBJCOPY)"

arch/sparc/boot_sparc32.o: arch/sparc/boot_sparc32.S bootinfo.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/boot.o: arch/sparc/boot.c bootinfo.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/kentry.o: kentry.c bootinfo.h config.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/ministubs.o: arch/sparc/ministubs.c
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/pad.o: arch/sparc/pad.S
	$(SPARC_CC) -c $< -o $@

arch/sparc/boot.elf: arch/sparc/pad.o arch/sparc/boot_sparc32.o arch/sparc/boot.o arch/sparc/kentry.o arch/sparc/ministubs.o
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_LDFLAGS) $^ -o $@

# Produce a.out SunOS big-endian client program variant (some OFs prefer this)
arch/sparc/boot.aout: arch/sparc/boot.elf
	@echo "[SPARC] Converting ELF to SunOS a.out (for OF netboot)..."
	@($(SPARC_OBJCOPY) -O a.out-sunos-big $< $@) >/dev/null 2>&1 || \
	 (echo "[SPARC] a.out-sunos-big unsupported on $(SPARC_OBJCOPY); trying a.out-sunos..." && \
	  ($(SPARC_OBJCOPY) -O a.out-sunos $< $@) >/dev/null 2>&1) || \
	 (echo "[SPARC] Trying host objcopy for SunOS a.out..." && \
	  ((command -v objcopy >/dev/null 2>&1 && objcopy -O a.out-sunos-big $< $@) >/dev/null 2>&1 || \
	   (command -v objcopy >/dev/null 2>&1 && objcopy -O a.out-sunos $< $@) >/dev/null 2>&1 || \
	   (command -v gobjcopy >/dev/null 2>&1 && gobjcopy -O a.out-sunos-big $< $@) >/dev/null 2>&1 || \
	   (command -v gobjcopy >/dev/null 2>&1 && gobjcopy -O a.out-sunos $< $@) >/dev/null 2>&1)) || \
	 (echo "[SPARC] WARNING: Neither cross nor host objcopy support SunOS a.out; copying ELF as fallback (netboot may fail)" && \
	  cp -f $< $@)

# Produce a raw binary variant for manual loading experiments
arch/sparc/boot.bin: arch/sparc/boot.elf
	@echo "[SPARC] Converting ELF to raw binary..."
	@($(SPARC_OBJCOPY) -O binary $< $@) >/dev/null 2>&1 || \
	 (echo "[SPARC] Cross objcopy failed, trying host objcopy..." && \
	  ((command -v objcopy >/dev/null 2>&1 && objcopy -O binary $< $@) >/dev/null 2>&1 || \
	   (command -v gobjcopy >/dev/null 2>&1 && gobjcopy -O binary $< $@) >/dev/null 2>&1))

# NOTE: Direct -kernel entry at 0x4000 is unreliable with some QEMU/OpenBIOS
# builds. Prefer OF client boot via TFTP using SunOS a.out format.
.PHONY: run-sparc
run-sparc: run-sparc-tftp

# Netboot (TFTP) for SPARC via OpenBIOS. This avoids -kernel path issues
# by letting OF fetch the image via its own TFTP client.
.PHONY: run-sparc-tftp
run-sparc-tftp: arch/sparc/boot.aout sparc-boot
	@mkdir -p tftp
	cp -f arch/sparc/boot.aout tftp/boot
	$(shell command -v qemu-system-sparc 2>/dev/null || echo qemu-system-sparc) \
		-M SS-5 -nographic -serial mon:stdio \
		-net nic,model=lance -net user,tftp=tftp,bootfile=boot \
		-boot n \
		-prom-env 'auto-boot?=true' \
		-prom-env 'boot-device=net' \
		-prom-env 'boot-file=' \
		-prom-env 'boot-command=boot net' \
		-prom-env 'input-device=ttya' -prom-env 'output-device=ttya'

# Create a CD-ROM ISO with the SPARC client program to boot via OpenBIOS
.PHONY: run-sparc-cdrom
run-sparc-cdrom: arch/sparc/boot.iso
	$(shell command -v qemu-system-sparc 2>/dev/null || echo qemu-system-sparc) \
		-M SS-5 -nographic -serial mon:stdio \
		-device scsi-cd,scsi-id=6,drive=cd0 \
		-drive if=none,id=cd0,file=arch/sparc/boot.iso,format=raw,media=cdrom \
		-boot d \
		-prom-env 'auto-boot?=true' \
		-prom-env 'input-device=ttya' -prom-env 'output-device=ttya'

# Direct -kernel load of client program (bypasses OF file loader)
.PHONY: run-sparc-kernel
run-sparc-kernel: arch/sparc/boot.elf
	$(shell command -v qemu-system-sparc 2>/dev/null || echo qemu-system-sparc) \
		-M SS-5 -nographic -serial mon:stdio \
		-kernel arch/sparc/boot.elf \
		-prom-env 'input-device=ttya' -prom-env 'output-device=ttya'

arch/sparc/boot.iso: arch/sparc/boot.aout
	@echo "[SPARC] Building ISO image with client program..."
	@rm -rf arch/sparc/cdroot && mkdir -p arch/sparc/cdroot
	cp -f arch/sparc/boot.aout arch/sparc/cdroot/boot
	@(command -v mkisofs >/dev/null 2>&1 && mkisofs -R -V MEZ-SPARC -o $@ arch/sparc/cdroot) || \
	 (command -v genisoimage >/dev/null 2>&1 && genisoimage -R -V MEZ-SPARC -o $@ arch/sparc/cdroot) || \
	 (command -v hdiutil >/dev/null 2>&1 && hdiutil makehybrid -iso -joliet -default-volume-name MEZ-SPARC -o $@ arch/sparc/cdroot >/dev/null 2>&1 && \
	  test -f $@ || mv $@.cdr $@)

.PHONY: all clean
display.o: display.c display.h console.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@
