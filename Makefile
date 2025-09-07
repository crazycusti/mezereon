# Compiler-Auswahl: Bevorzugt i386-elf-gcc (Cross-Compiler), dann gcc-15, dann gcc

# ARM-Macs: explizit i386-elf-gcc aus nativeos/i386-elf-toolchain verwenden
ifeq ($(shell uname),Darwin)
ifeq ($(shell uname -m),arm64)
CC := /opt/homebrew/bin/i386-elf-gcc
else
CC := $(shell command -v i386-elf-gcc 2>/dev/null || command -v gcc-15 2>/dev/null || echo gcc)
endif
else
CC := $(shell command -v gcc-15 2>/dev/null || echo gcc)
endif

AS = nasm
# Bevorzuge i386-elf-ld/objcopy falls verfügbar
LD := $(shell command -v i386-elf-ld 2>/dev/null || echo ld)
# Robust objcopy detection: try cross, then gobjcopy, then objcopy
OBJCOPY := $(shell command -v i386-elf-objcopy 2>/dev/null || command -v gobjcopy 2>/dev/null || command -v objcopy 2>/dev/null)
ifeq ($(strip $(OBJCOPY)),)
$(warning Kein objcopy gefunden. Bitte 'brew install i386-elf-binutils' oder 'brew install binutils' (gobjcopy) installieren.)
OBJCOPY := objcopy
endif
# If we fell back to plain 'objcopy', try to derive cross objcopy from CC path
ifeq ($(OBJCOPY),objcopy)
OBJCOPY := $(dir $(CC))$(patsubst %gcc,%objcopy,$(notdir $(CC)))
endif
CFLAGS ?= -ffreestanding -m32 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie
LDFLAGS ?= -Ttext 0x7E00 -m elf_i386

# On macOS, require cross i386-elf toolchain to avoid Mach-O/PIE issues
ifeq ($(shell uname),Darwin)
ifndef FORCE_HOST_TOOLS
ifeq ($(shell command -v i386-elf-gcc 2>/dev/null),)
$(error i386-elf-gcc not found. Install with: brew install i386-elf-gcc)
endif
ifeq ($(shell command -v i386-elf-ld 2>/dev/null),)
$(error i386-elf-ld not found. Install with: brew install i386-elf-binutils)
endif
endif
endif

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

ifeq ($(shell uname),Darwin)
ifeq ($(CC),gcc)
$(warning Weder i386-elf-gcc noch gcc-15 gefunden, benutze Standard gcc. Für beste Kompatibilität bitte 'brew install i386-elf-gcc' ausführen!)
endif
else
ifeq ($(CC),gcc)
$(warning gcc-15 nicht gefunden, benutze Standard gcc. Für beste Kompatibilität bitte gcc-15 installieren!)
endif
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


clean:
	rm -f *.o *.bin *.img netface.o console.o $(CONSOLE_BACKEND_OBJ) video.o main.o entry32.o isr.o idt.o interrupts.o kentry.o kernel_payload.bin bootloader.bin
	rm -f drivers/*.o
	rm -f arch/sparc/*.o arch/sparc/boot.elf

# Optional: build a SPARC32 OBP client boot stub (requires sparc-elf-gcc)
SPARC_CC ?= $(shell command -v sparc-elf-gcc 2>/dev/null || command -v sparc-unknown-elf-gcc 2>/dev/null || echo sparc-elf-gcc)
SPARC_OBJCOPY ?= $(shell command -v sparc-elf-objcopy 2>/dev/null || command -v sparc-unknown-elf-objcopy 2>/dev/null || command -v objcopy 2>/dev/null || echo objcopy)
# If we fell back to plain 'objcopy', try to derive the cross objcopy from SPARC_CC
ifeq ($(SPARC_OBJCOPY),objcopy)
SPARC_OBJCOPY := $(dir $(SPARC_CC))$(patsubst %gcc,%objcopy,$(notdir $(SPARC_CC)))
endif
SPARC_CFLAGS ?= -ffreestanding -nostdlib -Wall -Wextra -Os -mcpu=v8 -fno-pic -fno-pie -fno-builtin -mno-fpu -mflat -Wa,-Av8
SPARC_CDEFS ?= -DCONFIG_ARCH_X86=0 -DCONFIG_ARCH_SPARC=1
# Use absolute path for the linker script to avoid CWD issues
SPARC_LINKSCRIPT := $(abspath arch/sparc/link.ld)
SPARC_LDFLAGS ?= -nostdlib -Wl,-N,-T,$(SPARC_LINKSCRIPT)

.PHONY: sparc-boot
sparc-boot: arch/sparc/boot.elf
	ln -sf arch/sparc/boot.elf boot.elf

arch/sparc/boot_sparc32.o: arch/sparc/boot_sparc32.S bootinfo.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/boot.o: arch/sparc/boot.c bootinfo.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/kentry.o: kentry.c bootinfo.h config.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/minic.o: arch/sparc/minic.c
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/boot.elf: arch/sparc/boot_sparc32.o arch/sparc/boot.o arch/sparc/kentry.o
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
