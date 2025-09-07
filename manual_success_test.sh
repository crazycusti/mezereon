#!/bin/bash
# Manual test to prove the bootloader works

echo "=== MANUAL SUCCESS TEST ==="
echo "This test will prove the bootloader works by loading it manually"
echo ""
echo "INSTRUCTIONS:"
echo "1. QEMU will start with the OpenBIOS prompt"
echo "2. At the '0 >' prompt, type exactly these commands:"
echo ""
echo "   hex"
echo "   4000 constant load-addr"
echo "   load-addr ff alloc-mem constant mem-addr"  
echo "   \" /fd\" open-dev constant fd-ih"
echo "   mem-addr 10 fd-ih read-blocks"
echo "   mem-addr load-addr 10 move"
echo "   load-addr go"
echo ""
echo "   OR try this simpler approach:"
echo "   hex"
echo "   \" arch/sparc/boot.bin\" load"
echo "   go"
echo ""
echo "Press Ctrl+A then X to exit when done"
echo ""
echo "Starting OpenBIOS interactive session..."
sleep 3

# Start with floppy containing our bootloader
qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_working_disk.img,format=raw,if=floppy,index=0 \
    -prom-env 'auto-boot?=false' \
    -prom-env 'output-device=ttya' \
    -prom-env 'input-device=ttya'
