HTTP (minimal, static)

Overview
- Tiny HTTP/1.0 responder over our minimal TCP on IPv4.
- Single-connection, single-response: serves one payload and closes.
- Two modes:
  - inline: fixed body text (default)
  - file: serve a file from NeeleFS (v2) at a configured path

Commands
- http status
  - Prints state (CLOSED/LISTEN/ESTABLISHED/LAST_ACK) and port (default 80)
- http start [port]
  - Starts listening on TCP port (default 80)
- http stop
  - Stops the listener
- http body <text>
  - Sets inline response body (max ~512 bytes)
- http file </path>
  - Switches to file mode and sets source path (default "/www/index"). File is read from NeeleFS v2.
- http inline
  - Returns to inline mode

Example: Serve from NeeleFS
1) Create file on NeeleFS v2 (inside Mezereon shell):
   - neele mkfs; neele mount
   - neele mkdir /www
   - neele write /www/index.html "<html><body><h1>Hello</h1></body></html>"
2) Configure IP (example for QEMU usernet):
   - ip set 10.0.2.15 255.255.255.0 10.0.2.2
3) Start server and point to the file (default is already /www/index):
   - http file /www/index
   - http start 80
4) From host: curl http://10.0.2.15:80/

Notes & Limits
- No persistent connections (Connection: close always).
- Response is sent in a single TCP segment (max payload ~1460 bytes). Larger files are truncated.
- No retransmissions or reassembly; designed for local/QEMU testing.
- Only a single connection is handled at a time.
- Serving files requires NeeleFS v2 mounted; v1 is read-only and cannot be modified during runtime.
