#!/bin/bash
# Create proper SPARC bootable disk with ELF executable

echo "Creating SPARC bootable disk with ELF..."

# Method 1: Put the ELF file directly on the disk
echo "Method 1: ELF at sector 0"
cp arch/sparc/boot.elf sparc_elf_boot.img
dd if=/dev/zero bs=1 count=$((1440*1024 - $(stat -c%s arch/sparc/boot.elf))) >> sparc_elf_boot.img 2>/dev/null

# Method 2: Create a Sun disk with ELF after label
echo "Method 2: Sun labeled disk with ELF"
dd if=/dev/zero of=sparc_sun_elf.img bs=1440k count=1 2>/dev/null

# Write Sun disk label
python3 << 'PYTHON'
import struct

# Create a proper Sun disk label
label = bytearray(512)

# Sun magic number at the end
label[508:512] = struct.pack('>I', 0xDABE)

# Write some basic geometry info (simplified)
# Sector 0: disk label
# Sector 1: start of bootable code

with open('sparc_sun_elf.img', 'r+b') as f:
    f.write(label)
PYTHON

# Write ELF at sector 1
dd if=arch/sparc/boot.elf of=sparc_sun_elf.img bs=512 seek=1 conv=notrunc 2>/dev/null

# Method 3: Try a.out format (legacy SPARC format)
echo "Method 3: Convert to a.out format"
/home/wynton/sparc32gcc/.build/sparc-unknown-elf/buildtools/bin/sparc-unknown-elf-objcopy -O a.out-sunos-big arch/sparc/boot.elf arch/sparc/boot.aout 2>/dev/null || echo "a.out conversion failed"

if [ -f arch/sparc/boot.aout ]; then
    cp arch/sparc/boot.aout sparc_aout_boot.img
    dd if=/dev/zero bs=1 count=$((1440*1024 - $(stat -c%s arch/sparc/boot.aout))) >> sparc_aout_boot.img 2>/dev/null
fi

echo "Created bootable disk images:"
ls -la sparc_*_boot.img sparc_*_elf.img 2>/dev/null

echo -e "\nTesting the new disk formats..."
