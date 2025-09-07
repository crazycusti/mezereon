# Compiler-Auswahl: Standard gcc (host)
CC ?= gcc
AS = nasm
# Linker und Objcopy: host tools
LD ?= ld
# Robust objcopy detection (host first)
OBJCOPY := $(shell command -v objcopy 2>/dev/null || command -v gobjcopy 2>/dev/null || echo objcopy)
CFLAGS ?= -ffreestanding -m32 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie
LDFLAGS ?= -Ttext 0x7E00 -m elf_i386

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

bootloader.bin: bootloader.asm kernel_payload.bin
	ks=$$((($$(wc -c < kernel_payload.bin)+511)/512)); \
	$(AS) -f bin -D KERNEL_SECTORS=$$ks $< -o $@

entry32.o: entry32.asm
	$(AS) -f elf32 $< -o $@

isr.o: isr.asm
	$(AS) -f elf32 $< -o $@

main.o: main.c main.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

video.o: video.c main.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

console.o: console.c console.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

console_backend_vga.o: console_backend_vga.c console_backend.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

console_backend_fb_stub.o: console_backend_fb_stub.c console_backend.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

netface.o: netface.c netface.h config.h drivers/ne2000.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

platform.o: platform.c platform.h interrupts.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@
drivers/ne2000.o: drivers/ne2000.c drivers/ne2000.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/ata.o: drivers/ata.c drivers/ata.h config.h main.h keyboard.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/fs/neelefs.o: drivers/fs/neelefs.c drivers/fs/neelefs.h drivers/ata.h config.h main.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

keyboard.o: keyboard.c keyboard.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

shell.o: shell.c shell.h keyboard.h config.h main.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

cpu.o: cpu.c cpu.h console.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

kernel_payload.elf: entry32.o kentry.o isr.o idt.o interrupts.o platform.o main.o video.o console.o $(CONSOLE_BACKEND_OBJ) netface.o drivers/ne2000.o drivers/ata.o drivers/fs/neelefs.o keyboard.o cpu.o shell.o
	$(LD) $(LDFLAGS) $^ -o $@

# Erzeuge flaches Binary ohne führende 0x7E00-Lücke
kernel_payload.bin: kernel_payload.elf
	$(OBJCOPY) -O binary --change-addresses -0x7E00 $< $@

disk.img: bootloader.bin kernel_payload.bin
	cat $^ > $@

## --- QEMU run helpers (x86) ---
.PHONY: run-x86-floppy run-x86-hdd
run-x86-floppy: disk.img
	$(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		-drive file=disk.img,if=floppy,format=raw \
		-net none -serial stdio

run-x86-hdd: disk.img
	$(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		-drive file=disk.img,format=raw,if=ide \
		-net none -serial stdio

.PHONY: run-x86-hdd-ne2k
run-x86-hdd-ne2k: disk.img
	$(shell command -v qemu-system-i386 2>/dev/null || echo qemu-system-i386) \
		-drive file=disk.img,format=raw,if=ide \
		-device ne2k_isa,netdev=n0,io=$(CONFIG_NE2000_IO),irq=$(CONFIG_NE2000_IRQ) \
		-netdev user,id=n0 -serial stdio


# --- Help target ---
.PHONY: help
help:
	@echo "Mezereon — common make targets"
	@echo ""
	@echo "x86:"
	@echo "  make                  Build x86 floppy image (disk.img)"
	@echo "  make run-x86-floppy   Run QEMU with disk.img as floppy"
	@echo "  make run-x86-hdd      Run QEMU with disk.img as IDE disk (no NIC)"
	@echo "  make run-x86-hdd-ne2k Run QEMU IDE + NE2000 ISA (usernet)"
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
	rm -f *.o *.bin *.img netface.o console.o $(CONSOLE_BACKEND_OBJ) video.o main.o entry32.o isr.o idt.o interrupts.o kentry.o kernel_payload.bin kernel_payload.elf bootloader.bin
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
SPARC_LDFLAGS ?= -nostdlib -Wl,-N,-e,_start,-Ttext=0x4000

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

arch/sparc/boot.elf: arch/sparc/boot_sparc32.o arch/sparc/boot.o arch/sparc/kentry.o arch/sparc/ministubs.o
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
