#!/bin/bash
# Test interactive OpenBIOS boot

echo "=== Interactive OpenBIOS Test ==="
echo "This will start QEMU with OpenBIOS interactive mode."
echo "At the '0 >' prompt, try these commands:"
echo ""
echo "1. Basic info:"
echo "   .properties"
echo "   devalias"
echo "   probe-all"
echo ""
echo "2. Load our binary directly into memory:"
echo "   hex"
echo "   4000 constant load-base"
echo "   \" arch/sparc/boot.bin\" load-base load-file"
echo "   load-base go"
echo ""
echo "3. Alternative loading:"
echo "   \" arch/sparc/boot.bin\" load"
echo "   go"
echo ""
echo "Press Ctrl+A then X to exit"
echo ""
echo "Starting QEMU..."

# Start with our floppy disk attached but don't auto-boot
exec qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_boot_raw.img,format=raw,if=floppy,index=0 \
    -prom-env 'auto-boot?=false' \
    -prom-env 'output-device=ttya' \
    -prom-env 'input-device=ttya'
