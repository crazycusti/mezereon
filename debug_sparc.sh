#!/bin/bash
# Debug script for SPARC OpenBIOS issues

echo "=== SPARC Debug Information ==="
echo "QEMU version:"
qemu-system-sparc --version

echo -e "\nAvailable SPARC machines:"
qemu-system-sparc -M help | grep -i sparc

echo -e "\nGenerated files:"
ls -la arch/sparc/boot.*

echo -e "\nELF header:"
/home/wynton/sparc32gcc/.build/sparc-unknown-elf/buildtools/bin/sparc-unknown-elf-readelf -h arch/sparc/boot.elf

echo -e "\nFirst 32 bytes of binary:"
hexdump -C arch/sparc/boot.bin | head -2

echo -e "\n=== Testing with different approaches ==="

echo "1. Testing with SS-5 (original):"
timeout 8 qemu-system-sparc -M SS-5 -nographic \
    -kernel arch/sparc/boot.bin \
    -prom-env 'output-device=ttya' 2>&1 | head -20

echo -e "\n2. Testing with SPARCClassic:"
timeout 8 qemu-system-sparc -M SPARCClassic -nographic \
    -kernel arch/sparc/boot.bin 2>&1 | head -20

echo -e "\n3. Testing with SS-4:"
timeout 8 qemu-system-sparc -M SS-4 -nographic \
    -kernel arch/sparc/boot.bin 2>&1 | head -20

echo -e "\nFor manual debugging, run:"
echo "qemu-system-sparc -M SS-5 -nographic -monitor stdio"
echo "Then try loading manually from OpenBIOS prompt"
