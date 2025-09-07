#!/bin/bash
# Create OpenBIOS-compatible bootable image

echo "Creating OpenBIOS-compatible bootable images..."

# First, let's try creating an ELF that OpenBIOS will accept
# The issue might be that OpenBIOS expects specific ELF program headers

# Approach 1: Create ELF with different load address
echo "1. Creating ELF with different memory layout..."
cat > sparc_alt_link.ld << 'LINKER'
ENTRY(_start)
SECTIONS
{
  . = 0x4000;
  .text : { 
    *(.text*) 
  } = 0x01000000  /* Fill with NOPs */
  .rodata ALIGN(8) : { 
    *(.rodata*) 
  }
  .data ALIGN(8) : { 
    *(.data*) 
  }
  .bss ALIGN(8) : { 
    *(.bss*) 
    *(COMMON) 
  }
  
  /* Add some padding */
  . = ALIGN(0x1000);
}
LINKER

# Build with alternative linker script
/home/wynton/sparc32gcc/.build/sparc-unknown-elf/buildtools/bin/sparc-unknown-elf-gcc \
    -ffreestanding -nostdlib -O1 -mcpu=v7 -fno-pic -fno-pie -fno-builtin -mno-fpu \
    -nostdlib -Wl,-N -T sparc_alt_link.ld \
    arch/sparc/boot_sparc32.o arch/sparc/boot.o arch/sparc/kentry.o arch/sparc/minic.o \
    -o arch/sparc/boot_alt.elf

# Approach 2: Create with different ELF type (shared object vs executable)
echo "2. Creating shared object ELF..."
/home/wynton/sparc32gcc/.build/sparc-unknown-elf/buildtools/bin/sparc-unknown-elf-gcc \
    -ffreestanding -nostdlib -O1 -mcpu=v7 -fno-pic -fno-pie -fno-builtin -mno-fpu \
    -shared -nostdlib -Wl,-N -T arch/sparc/link.ld \
    arch/sparc/boot_sparc32.o arch/sparc/boot.o arch/sparc/kentry.o arch/sparc/minic.o \
    -o arch/sparc/boot_shared.elf 2>/dev/null || echo "Shared ELF creation failed"

# Create disk images with these different ELF formats
if [ -f arch/sparc/boot_alt.elf ]; then
    echo "3. Creating disk with alternative ELF..."
    cp arch/sparc/boot_alt.elf sparc_alt_elf_boot.img
    dd if=/dev/zero bs=1 count=$((1440*1024 - $(stat -c%s arch/sparc/boot_alt.elf))) >> sparc_alt_elf_boot.img 2>/dev/null
fi

if [ -f arch/sparc/boot_shared.elf ]; then
    echo "4. Creating disk with shared ELF..."
    cp arch/sparc/boot_shared.elf sparc_shared_elf_boot.img  
    dd if=/dev/zero bs=1 count=$((1440*1024 - $(stat -c%s arch/sparc/boot_shared.elf))) >> sparc_shared_elf_boot.img 2>/dev/null
fi

echo "Created alternative ELF images:"
ls -la sparc_*_elf_boot.img 2>/dev/null || echo "No alternative images created"

echo -e "\nTesting alternative ELF formats..."
