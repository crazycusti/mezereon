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
OBJCOPY := $(shell command -v i386-elf-objcopy 2>/dev/null || echo objcopy)
CFLAGS ?= -ffreestanding -m32 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie
LDFLAGS ?= -Ttext 0x7E00 -m elf_i386

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
	$(OBJCOPY) -O binary --binary-architecture i386 --change-addresses -0x7E00 $< $@

disk.img: bootloader.bin kernel_payload.bin
	cat $^ > $@


clean:
	rm -f *.o *.bin *.img netface.o console.o $(CONSOLE_BACKEND_OBJ) video.o main.o entry32.o isr.o idt.o interrupts.o kernel_payload.bin bootloader.bin
	rm -f drivers/*.o
	rm -f arch/sparc/*.o arch/sparc/boot.elf

# Optional: build a SPARC32 OBP client boot stub (requires sparc-elf-gcc)
SPARC_CC ?= $(shell command -v sparc-elf-gcc 2>/dev/null || echo sparc-elf-gcc)
SPARC_CFLAGS ?= -ffreestanding -nostdlib -Wall -Wextra -Os -mcpu=v8 -fno-pic -fno-pie -fno-builtin -mno-fpu -mflat
SPARC_CDEFS ?= -DCONFIG_ARCH_X86=0 -DCONFIG_ARCH_SPARC=1
SPARC_LDFLAGS ?= -nostdlib -Wl,-N -T arch/sparc/link.ld

.PHONY: sparc-boot
sparc-boot: arch/sparc/boot.elf
	ln -sf arch/sparc/boot.elf boot.elf

arch/sparc/boot_sparc32.o: arch/sparc/boot_sparc32.S arch/sparc/obp.h bootinfo.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/boot.o: arch/sparc/boot.c arch/sparc/obp.h bootinfo.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/kentry.o: kentry.c bootinfo.h config.h
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/minic.o: arch/sparc/minic.c
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_CDEFS) -c $< -o $@

arch/sparc/boot.elf: arch/sparc/boot_sparc32.o arch/sparc/boot.o arch/sparc/kentry.o arch/sparc/minic.o
	$(SPARC_CC) $(SPARC_CFLAGS) $(SPARC_LDFLAGS) $^ -o $@

.PHONY: run-sparc
run-sparc: sparc-boot
	$(shell command -v qemu-system-sparc 2>/dev/null || echo qemu-system-sparc) \
		-M SS-5 -nographic -serial mon:stdio \
		-prom-env 'output-device=ttya' -prom-env 'input-device=ttya' \
		-kernel arch/sparc/boot.elf

# Netboot (TFTP) for SPARC via OpenBIOS. This avoids -kernel path issues
# by letting OF fetch the image via its own TFTP client.
.PHONY: run-sparc-tftp
run-sparc-tftp: sparc-boot
	@mkdir -p tftp
	cp -f arch/sparc/boot.elf tftp/boot.elf
	$(shell command -v qemu-system-sparc 2>/dev/null || echo qemu-system-sparc) \
		-M SS-5 -nographic -serial mon:stdio \
		-net nic,model=lance -net user,tftp=tftp,bootfile=boot.elf \
		-prom-env 'boot-device=net' -prom-env 'input-device=ttya' -prom-env 'output-device=ttya'

.PHONY: all clean
