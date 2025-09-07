#!/bin/bash
# Create a bootable SPARC disk image

set -e

echo "Creating SPARC bootable disk image..."

# Create a blank floppy image (1.44MB)
dd if=/dev/zero of=sparc_boot.img bs=1440k count=1 2>/dev/null

# For SPARC, we need to create a simple disk that OpenBIOS can read
# SPARC systems often boot from the first sector of a disk

# Method 1: Raw binary at sector 0
echo "Method 1: Creating raw boot disk..."
dd if=arch/sparc/boot.bin of=sparc_boot_raw.img bs=512 conv=notrunc 2>/dev/null

# Method 2: Create a simple filesystem-like structure
echo "Method 2: Creating structured boot disk..."
cp arch/sparc/boot.bin sparc_boot_structured.img

# Method 3: Create a Sun disk label (SPARC-specific)
echo "Method 3: Creating Sun-labeled disk..."
dd if=/dev/zero of=sparc_boot_sun.img bs=1440k count=1 2>/dev/null

# Write our bootloader at offset 512 (after potential disk label)
dd if=arch/sparc/boot.bin of=sparc_boot_sun.img bs=512 seek=1 conv=notrunc 2>/dev/null

echo "Created disk images:"
ls -la sparc_boot_*.img

echo -e "\nTesting different boot approaches..."
