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
