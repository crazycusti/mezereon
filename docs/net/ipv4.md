IPv4 (rudimentary)

Overview
- Minimal IPv4 stack over Ethernet (NE2000):
  - ARP: learns peers; responds to ARP requests for our IP
  - IPv4: parses header; handles ICMP Echo Request (replies)
  - ICMP: can send Echo Requests (ping)

Configure
- Show: `ip`
- Set: `ip set <addr> <mask> [gw]`
  - Example (QEMU usernet): `ip set 10.0.2.15 255.255.255.0 10.0.2.2`

Ping
- `ip ping <addr> [count]`
  - Uses ARP to resolve target (or gateway if off-subnet) and sends ICMP Echo
  - Simple send-only (no RTT print yet); replies are handled by stack

Notes & Limits
- No DHCP, no TCP/UDP.
- ARP cache is small (8 entries), no ageing policy yet.
- ICMP Echo Reply is implemented; Echo Request sends are fire-and-forget with a basic wait loop.
- Frames larger than 1600 bytes are ignored by the RX path.
