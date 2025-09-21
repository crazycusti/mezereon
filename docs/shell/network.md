Network + HTTP helpers

- `netinfo` — NE2000 summary (MAC, IO/IRQ, promiscuous flag)
- `netrxdump` — dump raw Ethernet frames as they arrive; press `q` to exit; enable `CONFIG_NET_RX_DEBUG=1` in `config.h` for verbose driver-side logs
- `ip` — show IPv4 configuration; `ip set <ip> <mask> [gw]` configures static addresses; `ip ping <target> [count]` sends ICMP Echo
- `http [start|stop|status|body|file|inline]` — control the minimal HTTP demo (see `docs/net/http.md` for workflow)
