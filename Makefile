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
# Bevorzuge i386-elf-ld falls verfügbar
LD := $(shell command -v i386-elf-ld 2>/dev/null || echo ld)
CFLAGS ?= -ffreestanding -m32 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector -fno-pic -fno-pie
LDFLAGS ?= -Ttext 0x7E00 --oformat binary -m elf_i386

CONFIG_NE2000_IO ?= 0x300
CONFIG_NE2000_IRQ ?= 3
CONFIG_NE2000_IO_SIZE ?= 32

CDEFS = -DCONFIG_NE2000_IO=$(CONFIG_NE2000_IO) -DCONFIG_NE2000_IRQ=$(CONFIG_NE2000_IRQ) -DCONFIG_NE2000_IO_SIZE=$(CONFIG_NE2000_IO_SIZE)

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

main.o: main.c main.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

video.o: video.c main.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

network.o: network.c network.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/ne2000.o: drivers/ne2000.c drivers/ne2000.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/ata.o: drivers/ata.c drivers/ata.h config.h main.h keyboard.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

keyboard.o: keyboard.c keyboard.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

shell.o: shell.c shell.h keyboard.h config.h main.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

kernel_payload.bin: entry32.o main.o video.o network.o drivers/ne2000.o drivers/ata.o keyboard.o shell.o
	$(LD) -Ttext 0x7E00 --oformat binary -m elf_i386 $^ -o $@

disk.img: bootloader.bin kernel_payload.bin
	cat $^ > $@

clean:
	rm -f *.o *.bin *.img network.o video.o main.o entry32.o kernel_payload.bin bootloader.bin
	rm -f drivers/*.o

.PHONY: all clean
