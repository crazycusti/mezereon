#!/bin/bash
# Test different SPARC disk image boot methods

echo "=== Testing SPARC Disk Boot Methods ==="

test_disk() {
    local name="$1"
    local image="$2"
    local qemu_args="$3"
    
    echo -e "\n--- Testing $name ---"
    echo "Image: $image"
    echo "Args: $qemu_args"
    
    timeout 12 qemu-system-sparc $qemu_args "$image" || echo "Failed or timed out"
}

# Test 1: Floppy disk boot
test_disk "Floppy A" "sparc_boot_raw.img" "-M SS-5 -nographic -fda"

# Test 2: Hard disk boot  
test_disk "Hard Disk" "sparc_boot_structured.img" "-M SS-5 -nographic -hda"

# Test 3: Sun-labeled disk
test_disk "Sun Disk" "sparc_boot_sun.img" "-M SS-5 -nographic -hda"

# Test 4: CD-ROM boot
test_disk "CD-ROM" "sparc_boot_raw.img" "-M SS-5 -nographic -cdrom"

# Test 5: Different machine with floppy
test_disk "SPARCClassic Floppy" "sparc_boot_raw.img" "-M SPARCClassic -nographic -fda"

# Test 6: Boot from disk with specific boot order
test_disk "Hard Disk Boot Priority" "sparc_boot_structured.img" "-M SS-5 -nographic -boot c -hda"

echo -e "\n=== Alternative: Manual OpenBIOS approach ==="
echo "If automatic boot fails, try:"
echo "qemu-system-sparc -M SS-5 -nographic -fda sparc_boot_raw.img"
echo "Then at OpenBIOS prompt:"
echo "  boot floppy"
echo "  or: boot disk"
echo "  or: 4000 go (if already loaded)"
