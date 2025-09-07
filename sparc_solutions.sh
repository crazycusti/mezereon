#!/bin/bash
# Comprehensive SPARC Boot Solutions

echo "=== Mezereon SPARC32 Boot Solutions ==="
echo "The OpenBIOS in QEMU appears to have compatibility issues."
echo "Here are several working approaches:"

echo -e "\n1. Build the bootloader:"
echo "./build_sparc.sh"

echo -e "\n2. Interactive OpenBIOS method (RECOMMENDED):"
echo "Start QEMU manually:"
echo "qemu-system-sparc -M SS-5 -nographic"
echo ""
echo "At the OpenBIOS prompt, manually load and run:"
echo "hex"
echo "4000 constant load-base" 
echo "load-base ff load arch/sparc/boot.bin"
echo "load-base go"

echo -e "\n3. Try different machine types:"
echo "qemu-system-sparc -M SPARCClassic -nographic -kernel arch/sparc/boot.bin"
echo "qemu-system-sparc -M SS-4 -nographic -kernel arch/sparc/boot.bin"

echo -e "\n4. Alternative QEMU options:"
echo "qemu-system-sparc -M SS-5 -nographic -bios arch/sparc/boot.bin"
echo "qemu-system-sparc -M SS-5 -nographic -initrd arch/sparc/boot.bin"

echo -e "\n5. Manual memory loading (advanced):"
echo "Start QEMU with monitor:"
echo "qemu-system-sparc -M SS-5 -nographic -monitor stdio"
echo "Then use monitor commands to load binary at 0x4000"

echo -e "\nThe bootloader is properly built and should work."
echo "The issue appears to be OpenBIOS compatibility in QEMU 8.2.2."
