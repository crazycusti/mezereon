# SPARC Boot Issues and Solutions

## Problem Summary
The SPARC bootloader fails to execute in QEMU with OpenBIOS, showing:
```
Unhandled Exception 0x00000002
PC = 0x00004000 NPC = 0x00004004  (or PC = 0xffda042c)
```

## Root Cause Analysis
1. **Exception 0x02**: Illegal Instruction trap
2. **OpenBIOS Compatibility**: QEMU's OpenBIOS expects specific kernel protocols
3. **ELF vs Binary**: OpenBIOS has issues with our ELF format but works better with raw binaries

## Solutions Implemented

### 1. Binary Format Support
- Modified Makefile to generate both `.elf` and `.bin` formats
- Updated `run-sparc` target to use binary format
- Binary format reduces OpenBIOS compatibility issues

### 2. Improved Assembly Entry Point
- Better register window management
- Proper stack alignment (16-byte aligned)
- More conservative OpenFirmware client interface handling

### 3. Enhanced C Code
- Added null pointer checks
- Better error handling for OpenFirmware calls
- More robust console detection and output

## Alternative Boot Methods

If the standard approach fails, try these alternatives:

### Method 1: Different QEMU Machine
```bash
qemu-system-sparc -M SPARCClassic -nographic -kernel arch/sparc/boot.bin
```

### Method 2: Manual OpenBIOS Loading
```bash
qemu-system-sparc -M SS-5 -nographic -prom-env 'auto-boot?=false'
# Then at the OpenBIOS prompt:
# 4000 6000 load arch/sparc/boot.bin
# 4000 go
```

### Method 3: Use initrd instead
```bash
qemu-system-sparc -M SS-5 -nographic -initrd arch/sparc/boot.bin
```

## Known Issues
- OpenBIOS v1.1 in QEMU may have compatibility issues with custom kernels
- Some SPARC instruction encodings may not be properly supported
- Memory management and privilege levels might need adjustment

## Troubleshooting
- Check that SPARC toolchain is properly installed
- Verify binary format is being used (not ELF)
- Try different QEMU machine types (SS-4, SS-10, SPARCClassic)
- Consider using newer QEMU versions or different OpenBIOS builds
