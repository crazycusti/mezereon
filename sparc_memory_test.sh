#!/bin/bash
# Direct memory loading test for SPARC bootloader

echo "=== Direct Memory Loading Test ==="

# Create a simple GDB script to load and test our bootloader
cat > test_bootloader.gdb << 'GDB_SCRIPT'
# Connect to QEMU GDB server
target remote :1234

# Load our binary into memory at 0x4000  
restore arch/sparc/boot.bin binary 0x4000

# Set PC to entry point
set $pc = 0x4000
set $npc = 0x4004

# Show registers
info registers

# Single step a few instructions to see if they work
stepi
info registers
stepi  
info registers
stepi
info registers

# Continue execution
continue
GDB_SCRIPT

echo "Starting QEMU with GDB server..."
echo "In another terminal, run: gdb -x test_bootloader.gdb"

# Start QEMU with GDB server and wait for connection
timeout 30 qemu-system-sparc -M SS-5 -nographic -s -S \
    -prom-env 'output-device=ttya' \
    -prom-env 'input-device=ttya' &

QEMU_PID=$!
sleep 2

echo "QEMU started with PID $QEMU_PID"
echo "Now running GDB test..."

if command -v gdb >/dev/null 2>&1; then
    # Try to run GDB with our script
    timeout 20 gdb -batch -x test_bootloader.gdb 2>&1 || echo "GDB test completed"
else
    echo "GDB not available. QEMU is running with GDB server on port 1234"
    echo "Connect manually with: gdb -ex 'target remote :1234'"
    wait $QEMU_PID
fi

# Clean up
kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true
