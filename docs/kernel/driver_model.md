# Mezereon Unified Driver Model (UDM) - Architectural Blueprint

## Objective
To replace hardcoded `if/else` logic with a scalable, capability-based driver registration system. This enables Mezereon to support a vast array of hardware (GPU, Net, Audio, Storage) without bloat in the core kernel.

## Core Concepts

### 1. The Generic Device Object (`struct device`)
Every hardware component discovered by the kernel is represented as a `device` instance.
- **Metadata:** Name, Bus Type (PCI, ISA, LPC).
- **Binding:** A pointer to the active `gpu_driver_t`, `net_driver_t`, etc.
- **Storage:** Opaque `driver_private` pointer for state.

### 2. The Driver Registry
Treiber registrieren sich beim Booten in einem globalen Array.
- **Match Function:** `int (*match)(device_t* dev)` returns a priority score.
- **Probe/Attach:** `int (*attach)(device_t* dev)` initializes the hardware.

### 3. Subsystem Interfaces (The Contract)
Der Kernel greift nie direkt auf Hardware-Register zu.
- **GPU Interface:** `set_mode`, `sync`, `get_caps`, `restore_text`.
- **Audio Interface:** `play_buffer`, `stop`, `set_volume`.
- **Net Interface:** `send_pkt`, `recv_pkt`, `get_stats`.

## Implementation Roadmap

### Phase 1: GPU Registry (Immediate Priority)
- Move existing drivers (AVGA2, SMOS, ET4000) into a `g_gpu_drivers` registry.
- Replace `main.c` string checks with `gpu_find_best_driver()`.

### Phase 2: HAL Integration
- Standardize the `fb_accel` and `shadow_sync` logic.
- Drivers only provide the `bank_switch` and `plane_setup` callbacks.

### Phase 3: Unified Bus Scanning
- The PCI scanner populates the device list.
- ISA drivers perform "legacy probing" and populate the list.
- The kernel runs a `device_auto_bind()` loop.
