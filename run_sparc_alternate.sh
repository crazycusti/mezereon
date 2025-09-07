#!/usr/bin/env bash
# Deprecated helper. Use Makefile targets instead.
set -e
echo "[deprecated] Prefer: make run-sparc-tftp"
make sparc-boot
make run-sparc-tftp
