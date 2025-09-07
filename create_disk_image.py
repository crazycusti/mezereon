#!/usr/bin/env python3
# Create SPARC bootable disk image

import struct
import sys

if len(sys.argv) != 3:
    print("Usage: create_disk_image.py <bootloader.bin> <output.img>")
    sys.exit(1)

bootloader_file = sys.argv[1]
output_file = sys.argv[2]

# Create 1.44MB disk
size = 1440 * 1024
disk = bytearray(size)

# Add Sun disk label magic
struct.pack_into('>I', disk, 508, 0xDABE)
struct.pack_into('>H', disk, 510, 0x01)  # Bootable flag

# Read bootloader
with open(bootloader_file, 'rb') as f:
    bootloader = f.read()

# Place bootloader at sector 1 (offset 512)
disk[512:512+len(bootloader)] = bootloader

# Write disk image
with open(output_file, 'wb') as f:
    f.write(disk)

print(f"Created SPARC disk: {len(bootloader)} bytes bootloader in {size} byte disk")
