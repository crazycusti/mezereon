#!/usr/bin/env bash
# Deprecated helper. Use Makefile targets instead.
set -e
echo "[deprecated] Prefer: make run-sparc-tftp or run-sparc-kernel"
make sparc-boot
