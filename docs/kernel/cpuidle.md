CPU Idle (x86 HLT)

Overview
- A simple CPU idle helper is provided for x86 using the `HLT` instruction.
- It reduces busy-wait spinning by halting the CPU until the next interrupt.

API
- `cpuidle_init()` → currently a no-op (placeholder for future policies)
- `cpuidle_idle()` → executes `HLT` on x86; no-op on other arches
- `cpuidle_wakeups_get()` → returns number of HLT wakeups since init

Integration points
- Shell main loop: when no key is pending, the kernel calls `cpuidle_idle()` instead of tight spinning.
- `atadump`: while waiting for navigation keys, halts between polls.
- `netrxdump`: halts between keyboard checks while polling RX.
- Status bar (IRQ0): shows `T <secs>s I <wakeups>` right-aligned; updated ~10 Hz.

Shell helpers
- `ticks` → prints raw PIT tick count since boot.
- `wakeups` → prints accumulated HLT wakeups since `cpuidle_init()` (boot).

Notes
- IRQs must be enabled for `HLT` to resume on timer/keyboard/network interrupts (the kernel enables them during boot).
- This is a minimal policy; later we can add deeper C-states, MWAIT/monitor, and per-arch backends.
