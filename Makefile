CC ?= gcc
AS = nasm
LD ?= ld
CFLAGS ?= -ffreestanding -m32 -Wall -Wextra -nostdlib -fno-builtin -fno-stack-protector
LDFLAGS ?= -Ttext 0x7E00 --oformat binary

CONFIG_NE2000_IO ?= 0x300
CONFIG_NE2000_IRQ ?= 3
CONFIG_NE2000_IO_SIZE ?= 32

CDEFS = -DCONFIG_NE2000_IO=$(CONFIG_NE2000_IO) -DCONFIG_NE2000_IRQ=$(CONFIG_NE2000_IRQ) -DCONFIG_NE2000_IO_SIZE=$(CONFIG_NE2000_IO_SIZE)

all: disk.img

bootloader.bin: bootloader.asm
	$(AS) -f bin $< -o $@

main.o: main.c main.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

video.o: video.c main.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

network.o: network.c network.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

drivers/ne2000.o: drivers/ne2000.c drivers/ne2000.h config.h
	$(CC) $(CFLAGS) $(CDEFS) -c $< -o $@

kernel.bin: main.o video.o network.o drivers/ne2000.o
	$(LD) $(LDFLAGS) $^ -o $@

disk.img: bootloader.bin kernel.bin
	cat $^ > $@

clean:
	rm -f *.o *.bin *.img network.o video.o main.o kernel.bin bootloader.bin
	rm -f drivers/*.o

.PHONY: all clean
