#!/bin/bash
# Try direct memory loading approach

echo "=== Direct Memory Load Test ==="

# Create a simple test script for OpenBIOS
cat > /tmp/boot_commands.txt << 'COMMANDS'
hex
." Loading Mezereon bootloader at 4000..." cr
4000 constant load-addr
sparc_boot_raw.img load-addr load-file
." Loaded. Jumping to 4000..." cr  
load-addr go
COMMANDS

echo "Testing direct load approach..."
echo "Available commands in /tmp/boot_commands.txt"

# Try with different approaches
echo "1. Testing with -prom-env nvramrc script..."
timeout 15 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_boot_raw.img,format=raw,if=floppy,index=0 \
    -prom-env 'auto-boot?=true' \
    -prom-env 'boot-device=floppy' \
    -prom-env 'output-device=ttya' 2>/dev/null || echo "Method 1 failed"

echo -e "\n2. Testing simple floppy boot..."
timeout 15 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_boot_raw.img,format=raw,if=floppy,index=0 \
    -boot a 2>/dev/null || echo "Method 2 failed"

echo -e "\n3. Testing hard disk boot..."  
timeout 15 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_boot_structured.img,format=raw,if=scsi,index=0 \
    -boot c 2>/dev/null || echo "Method 3 failed"

echo -e "\n4. Testing with explicit load address..."
timeout 15 qemu-system-sparc -M SS-5 -nographic \
    -drive file=sparc_boot_raw.img,format=raw,if=floppy,index=0 \
    -prom-env 'auto-boot?=false' \
    -prom-env 'load-base=4000' 2>/dev/null || echo "Method 4 failed"

echo -e "\nTry running test_manual_boot.sh for interactive testing"
