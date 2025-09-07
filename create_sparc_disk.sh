#!/usr/bin/env bash
# Deprecated helper. Use Makefile targets instead.
set -e
echo "[deprecated] Prefer: make run-sparc-tftp or run-sparc-cdrom"
echo "Building SPARC artifacts..."
make sparc-boot
echo "For netboot: make run-sparc-tftp"
