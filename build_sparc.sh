#!/usr/bin/env bash
# Deprecated helper. Use Makefile targets instead.
set -e
echo "[deprecated] Use: make sparc-boot (and run-sparc-tftp)"
make sparc-boot
echo "Run with: make run-sparc-tftp"
