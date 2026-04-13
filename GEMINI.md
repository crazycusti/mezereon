# Mezereon Development Mandates (AI Guidance)

## Core Architectural Mandates
1. **Interface Over Hardcoding:** Never add new `if/else` checks for specific hardware in `main.c` or core subsystems. 
2. **Modular Driver Model:** All new drivers must follow the **Unified Driver Model (UDM)**. They must register via a driver object and provide capability-based scores.
3. **Abstraction Layer:** Subsystems (GPU, Net, Audio) must provide a Hardware Abstraction Layer (HAL). Core code only calls the HAL, never hardware-specific functions.
4. **Memory Safety:** Large buffers (Shadow Framebuffers, Network stacks) MUST be dynamically allocated via `memory_alloc` to avoid clobbering low memory (below 1MB).
5. **Robust Restoration:** Every driver must implement a "Text Mode Restoration" path to ensure the system remains usable after exiting graphical applications.

## Technical Standards
- **Wait/Sync:** Always implement a `sync` or `flush` mechanism for slow buses (ISA/Legacy) to prevent CPU stalls.
- **Diagnostics:** Every driver must provide a state-dumping function for the `gpudump` / `netinfo` shell commands.
- **Compatibility:** Favor standard VGA modes (12h, 13h, 3h) as reliable fallbacks when SVGA detection is uncertain.

## Project Vision
Mezereon aims to be a robust, modular OS for legacy hardware (386/486/68k). Scalability via a **Unified Device Tree** is the primary technical goal for v0.6+.

*See `docs/kernel/driver_model.md` for full technical details.*
