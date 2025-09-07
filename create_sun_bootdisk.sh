#!/bin/bash
# Create a proper Sun-style bootable disk for SPARC

echo "Creating Sun-style bootable disk..."

# SPARC systems use different boot conventions than x86
# Sun systems expect a disk label and boot block structure

# Create a 1.44MB floppy image
dd if=/dev/zero of=sun_boot_disk.img bs=1440k count=1 2>/dev/null

# Create a simple Sun disk label at the beginning
# Sun disk label is 512 bytes, bootable code starts after that
python3 << 'PYTHON'
import struct

# Create a minimal Sun disk label
# This is a simplified version - real Sun labels are more complex
label = bytearray(512)

# Sun disk label magic bytes (simplified)
label[508:512] = struct.pack('>I', 0xDABE)  # Sun magic

# Write to file
with open('sun_boot_disk.img', 'r+b') as f:
    f.write(label)
PYTHON

# Now write our bootloader after the disk label (at offset 512)
dd if=arch/sparc/boot.bin of=sun_boot_disk.img bs=512 seek=1 conv=notrunc 2>/dev/null

echo "Created Sun-style disk image: sun_boot_disk.img"
ls -la sun_boot_disk.img

echo "Testing Sun disk boot..."
