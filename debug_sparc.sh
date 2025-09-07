#!/usr/bin/env bash
# Deprecated: use Makefile targets (run-sparc-tftp / run-sparc-kernel)
set -e
echo "[deprecated] Prefer: make run-sparc-tftp"
make sparc-boot
