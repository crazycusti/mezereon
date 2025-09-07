#!/bin/bash
# Test loading via QEMU monitor

echo "=== QEMU Monitor Loading Test ==="
echo "This approach uses QEMU monitor to load the binary directly into memory"

# Create expect script for automated testing
cat > monitor_test.exp << 'SCRIPT'
#!/usr/bin/expect -f
set timeout 15

# Start QEMU with monitor
spawn qemu-system-sparc -M SS-5 -nographic -monitor stdio -S

# Wait for monitor prompt
expect "(qemu) "

# Load binary into memory at 0x4000
send "x/i 0x0\r"
expect "(qemu) "

# Load our binary into memory
send "loadvm arch/sparc/boot.bin 0x4000\r"
expect "(qemu) " 

# Set PC to our entry point and continue
send "cpu 0\r"
expect "(qemu) "
send "info registers\r"
expect "(qemu) "
send "set \$pc=0x4000\r"
expect "(qemu) "
send "continue\r"

# Wait for output or timeout
expect timeout {
    send_user "Test completed or timed out\n"
}
SCRIPT

chmod +x monitor_test.exp

echo "Running monitor test..."
if command -v expect >/dev/null 2>&1; then
    ./monitor_test.exp
else
    echo "expect not available, running manual monitor test..."
    echo "Starting QEMU with monitor. Try these commands:"
    echo "  x/i 0x4000"
    echo "  set \$pc=0x4000" 
    echo "  continue"
    
    timeout 10 qemu-system-sparc -M SS-5 -nographic -monitor stdio -S
fi
