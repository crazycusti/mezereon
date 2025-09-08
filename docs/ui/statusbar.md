Status Bar (Top Row)

Overview
- The entire top row is a blue status bar used for ephemeral UI: time, warnings, network info.
- Right side: periodic timer display (e.g., `T 123.4s`).
- Left side: free text for current status/errors (e.g., `pad: checksum mismatch`).

Behavior
- Repaints the full top row on each status update or timer tick; console text area remains unaffected.
- The normal text output starts from the second row (depending on boot screen preservation).

API (kernel)
- Set left text: `console_status_set_left(const char* s)`
  - Clears previous left text when passing an empty string.
- Set right text: internal; timer ISR calls `console_draw_status_right(buf, len)` via backend mapping.

Notes
- VGA text mode: blue background attribute `0x1F` is used.
- Designed to host future data like IP address, link state, mount status, etc.

Examples
- On CRC error in pad: left shows `pad: checksum mismatch` while the editor opens an empty buffer.
- On automount success: left shows `storage: v2@2048` at boot.
- Future: on network init, display `eth0: 10.0.2.15` or `link down`.
