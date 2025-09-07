#!/bin/bash

# Create a temporary OpenBIOS script to load our kernel
cat > /tmp/boot_script.fth << 'SCRIPT'
." Mezereon SPARC32 Bootloader" cr
." Loading kernel manually..." cr
hex 4000 constant load-base
SCRIPT

# Try different approaches to boot
echo "=== Approach 1: Direct kernel load ==="
timeout 10 qemu-system-sparc -M SS-5 -nographic \
    -prom-env 'auto-boot?=false' \
    -prom-env 'output-device=ttya' \
    -prom-env 'input-device=ttya' \
    -kernel arch/sparc/boot.bin || echo "Approach 1 failed"

echo -e "\n=== Approach 2: Try SPARCstation 4 ==="
timeout 10 qemu-system-sparc -M SS-4 -nographic \
    -serial mon:stdio \
    -prom-env 'output-device=ttya' \
    -prom-env 'input-device=ttya' \
    -kernel arch/sparc/boot.bin || echo "Approach 2 failed"

echo -e "\n=== Approach 3: Try without prom-env ==="
timeout 10 qemu-system-sparc -M SS-5 -nographic \
    -kernel arch/sparc/boot.bin || echo "Approach 3 failed"
