# Serial Debug Plugin (COM1)

The serial debug plugin is enabled by default through `CONFIG_DEBUG_SERIAL_PLUGIN` and
initializes the first PC-compatible UART (COM1) very early during boot so that
boot diagnostics appear on the serial line in parallel to the screen output.

## Hardware/Emulator Requirements

To observe output on COM1 you need:

1. **8250/16450/16550-compatible UART at 0x3F8.**
   The plugin programs the classic COM1 base address configured by
   `CONFIG_DEBUG_SERIAL_PORT` (default `0x3F8`). If your hardware maps the
   primary UART elsewhere, adjust this constant.
2. **Clocked UART reference.**
   The initialization writes the divisor latch for a 9600 baud rate assuming the
   standard 1.8432 MHz UART reference clock used by PC serial controllers. Most
   ISA/PCI cards and emulators (including QEMU/Bochs/VirtualBox) provide this by
   default.
3. **No additional IRQ routing.**
   Transmission is done with polling (`inb` on the line status register until the
   THR-empty bit is set). The plugin does **not** rely on a UART interrupt, so you
   do not have to unmask IRQ4 or configure PIC routing just to get characters out.
   (The periodic heartbeat uses the PIT IRQ0 that the kernel already enables.)
4. **Terminal configured for 9600 8N1.**
   Connect a null-modem cable or your emulator’s virtual serial port to a host
   terminal set to 9600 baud, 8 data bits, no parity, and one stop bit. Hardware
   flow control is not used.

Once these prerequisites are satisfied, all console traffic and the heartbeat
messages appear on COM1 starting at the boot banner. If you disable the plugin by
setting `CONFIG_DEBUG_SERIAL_PLUGIN` to `0`, the serial UART is left untouched.
