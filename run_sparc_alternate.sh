#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Run automated bootloader smoke tests for both LBA and CHS paths
"$SCRIPT_DIR/test_manual_boot.sh" --headless --lba --timeout 8 "$@"
"$SCRIPT_DIR/test_manual_boot.sh" --headless --chs --timeout 8 "$@"
