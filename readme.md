# Mezereon OS 🌿 v0.5.3-AeroAcumos

Mezereon is a modular hobby operating system designed for legacy x86 (386/486) and SPARC hardware. It focuses on stability, hardware discovery, and providing a modern development experience on classic chips.

## 🚀 Key Features
- **📺 Graphics:** Robust SVGA support for **Cirrus Logic (AVGA2)**, **Tseng ET4000**, and **SMOS SPC8106 (Compaq Aero)**.
  - *New in v0.5.3:* High-speed **32-bit VRAM sync** for ISA cards and full-height 640x480 console support.
- **⌨️ Input & Control:** PS/2 Keyboard support and a fast, bi-directional Serial Shell (115200 8N1).
- **🌐 Networking:** Integrated TCP/IP stack with NE2000 (ISA) support and a built-in HTTP server.
- **💾 Storage:** ATA PIO (LBA28) driver with **NeeleFS v2** (Read/Write filesystem).
- **🔊 Audio:** PC Speaker beeps and SoundBlaster 16 (SB16) driver.
- **🏗️ Architecture:** Features a **Unified Driver Model (UDM)** and Hardware Abstraction Layer (HAL) for clean, modular hardware support.

## 🛠️ Quick Start
1. **Build the OS:**
   ```bash
   make
   ```
2. **Run in QEMU (Graphics):**
   ```bash
   make run-x86-fb
   ```
3. **Run in QEMU (Serial Console):**
   ```bash
   make run-x86-hdd
   ```

## 🏛️ Legacy Hardware Optimization
Mezereon is specifically tuned for real 386DX/486 systems:
- **Shadow Buffering:** Minimizes ISA bus bottlenecks.
- **Memory Safety:** Kernel and buffers are carefully placed to avoid clobbering BIOS/EBDA.
- **Text Mode Recovery:** Guaranteed clean return to the shell after exiting graphical apps.

## 📂 Project Structure
- `drivers/gpu/`: Hardware-specific SVGA drivers.
- `net/`: Minimal TCP/IP stack.
- `apps/`: Built-in demos like `rotcube` (3D) and `keymusic`.
- `docs/`: Extensive documentation on kernel internals and hardware.

---
*Crafted for the silicon of yesteryear.* 💎
