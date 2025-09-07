#!/bin/bash
# Test manual OpenBIOS boot

echo "=== Manual OpenBIOS Boot Test ==="
echo "Starting QEMU with floppy disk..."
echo "You should see the OpenBIOS prompt."
echo "Try these commands at the prompt:"
echo "  probe-all"
echo "  devalias" 
echo "  boot floppy"
echo "  or: hex 4000 go"
echo ""
echo "Press Ctrl+A then X to exit QEMU"
echo ""

# Add format specification to avoid warnings
qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_boot_raw.img,format=raw,if=floppy,index=0 \
    -prom-env 'auto-boot?=false' \
    -prom-env 'output-device=ttya' \
    -prom-env 'input-device=ttya'
