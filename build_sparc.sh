#!/bin/bash
# Mezereon SPARC Build and Test Script

set -e

# Configuration
SPARC_TOOLCHAIN="/home/wynton/sparc32gcc/.build/sparc-unknown-elf/buildtools/bin"
SPARC_CC="${SPARC_TOOLCHAIN}/sparc-unknown-elf-gcc"
SPARC_OBJCOPY="${SPARC_TOOLCHAIN}/sparc-unknown-elf-objcopy"

# Compiler flags optimized for OpenBIOS compatibility
SPARC_CFLAGS="-ffreestanding -nostdlib -Wall -Wextra -O1 -mcpu=v7 -fno-pic -fno-pie -fno-builtin -mno-fpu"

echo "Building SPARC bootloader..."

# Clean previous builds
rm -f arch/sparc/*.o arch/sparc/boot.elf arch/sparc/boot.bin boot.elf

# Build with optimized settings
if SPARC_CC="$SPARC_CC" SPARC_CFLAGS="$SPARC_CFLAGS" make sparc-boot; then
    echo "Build successful!"
    echo "Generated files:"
    ls -la arch/sparc/boot.elf arch/sparc/boot.bin
    
    echo -e "\nDisassembly (first 20 lines):"
    ${SPARC_TOOLCHAIN}/sparc-unknown-elf-objdump -d arch/sparc/boot.elf | head -20
    
    echo -e "\nTo test, run:"
    echo "  make run-sparc"
    echo "  or try: ./run_sparc_alternate.sh"
else
    echo "Build failed!"
    exit 1
fi
