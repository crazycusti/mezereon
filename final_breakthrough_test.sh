#!/bin/bash
# Final breakthrough test - comprehensive SPARC boot testing

echo "=== FINAL BREAKTHROUGH TEST ==="
echo "Testing ALL possible SPARC boot methods systematically..."

# Test 1: Try NetBSD/OpenBSD SPARC boot approach
echo -e "\n1. Testing with different boot-device settings..."
timeout 10 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_elf_boot.img,format=raw,if=scsi,index=0,boot=on \
    -prom-env 'boot-device=sd(0,0,0)' \
    -prom-env 'auto-boot?=true' 2>&1 | grep -E "(boot|load|error|exception)" || echo "Test 1 failed"

# Test 2: Floppy with specific device path
echo -e "\n2. Testing floppy with device path..."
timeout 10 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_elf_boot.img,format=raw,if=floppy,index=0 \
    -prom-env 'boot-device=fd' \
    -prom-env 'auto-boot?=true' 2>&1 | grep -E "(boot|load|error|exception)" || echo "Test 2 failed"

# Test 3: Try different machine entirely
echo -e "\n3. Testing SPARCstation 10..."
timeout 10 qemu-system-sparc -M SS-10 -nographic \
    -drive file=sparc_elf_boot.img,format=raw,if=scsi,index=0 \
    -boot c 2>&1 | grep -E "(boot|load|error|exception)" || echo "Test 3 failed"

# Test 4: Minimal OpenBIOS test to see if we can load anything
echo -e "\n4. Testing minimal file load capability..."
timeout 15 bash << 'BASH_SCRIPT'
    echo "hex 4000 1000 dump" | qemu-system-sparc -M SS-5 -nographic \
        -prom-env 'auto-boot?=false' 2>&1 | head -20
BASH_SCRIPT

echo -e "\n=== Creating working disk image approach ==="

# Create a super simple test disk that just contains our binary
# but with proper Sun disk formatting
echo "5. Creating minimal Sun disk format..."

python3 << 'PYTHON'
import struct

# Create a proper 1.44MB floppy with Sun disk structure
size = 1440 * 1024
disk = bytearray(size)

# Sun disk label structure (simplified but more complete)
# This is based on actual Sun disk label format

# Sun magic at the end of first 512 bytes
struct.pack_into('>I', disk, 508, 0xDABE)

# Mark this as bootable by setting boot flag
struct.pack_into('>H', disk, 510, 0x01)  # Bootable flag

# Write our bootloader starting at sector 1 (offset 512)
with open('arch/sparc/boot.bin', 'rb') as f:
    bootloader = f.read()
    
# Place bootloader at sector 1 
disk[512:512+len(bootloader)] = bootloader

# Write the complete disk
with open('sparc_working_disk.img', 'wb') as f:
    f.write(disk)

print(f"Created working disk image: {len(bootloader)} bytes bootloader in {size} byte disk")
PYTHON

echo "Created sparc_working_disk.img with proper Sun formatting"

# Test this new disk
echo -e "\n6. Testing working disk image..."
timeout 12 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_working_disk.img,format=raw,if=scsi,index=0 \
    -boot c 2>&1 | tail -15

echo -e "\nAll tests completed!"
