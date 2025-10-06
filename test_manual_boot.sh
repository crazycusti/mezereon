#!/usr/bin/env bash
set -euo pipefail

MODE="lba"
HEADLESS=0
TIMEOUT="${TIMEOUT:-10}"
QEMU_EXTRA=()

usage() {
    cat <<USAGE
Usage: $0 [--lba|--chs|--both] [--headless] [--timeout SECONDS] [--] [qemu-args...]

Options:
  --lba        Build the image with default LBA path (default behaviour)
  --chs        Rebuild Stage 2 forcing CHS reads, then run the emulator
  --both       Run both LBA and CHS variants sequentially
  --headless   Run QEMU without a window (serial log to stdout, auto-timeout)
  --timeout    Override timeout (seconds) when --headless is supplied (default: ${TIMEOUT:-10})
  --help       Show this message
  --           Remaining arguments are passed to QEMU
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --lba)
            MODE="lba"
            shift
            ;;
        --chs)
            MODE="chs"
            shift
            ;;
        --both)
            MODE="both"
            shift
            ;;
        --headless)
            HEADLESS=1
            shift
            ;;
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            QEMU_EXTRA=("$@")
            break
            ;;
        *)
            echo "[error] Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

find_qemu() {
    if [[ -n "${QEMU_BIN:-}" ]]; then
        echo "$QEMU_BIN"
        return
    fi
    if command -v qemu-system-i386 >/dev/null 2>&1; then
        command -v qemu-system-i386
        return
    fi
    if command -v qemu-system-x86_64 >/dev/null 2>&1; then
        command -v qemu-system-x86_64
        return
    fi
    echo "[error] qemu-system-i386 not found" >&2
    exit 1
}

QEMU=$(find_qemu)

build_image() {
    local force_chs=$1
    if [[ $force_chs -eq 1 ]]; then
        echo "[build] Rebuilding disk image (Stage 2 forced to CHS)"
    else
        echo "[build] Rebuilding disk image (default LBA path)"
    fi
    make -B stage1.bin stage2.bin bootloader.bin disk.img STAGE2_FORCE_CHS=$force_chs >/dev/null
}

launch_qemu() {
    local label=$1
    local force_chs=$2

    build_image "$force_chs"

    echo "[run] Starting QEMU ($label)"
    local args=("$QEMU" -drive file=disk.img,format=raw,if=ide -serial stdio)
    if [[ $HEADLESS -eq 1 ]]; then
        args+=( -display none -monitor none -no-reboot -no-shutdown )
        if ! timeout "${TIMEOUT}s" "${args[@]}" "${QEMU_EXTRA[@]}"; then
            local rc=$?
            if [[ $rc -eq 124 ]]; then
                echo "[info] Headless run timed out after ${TIMEOUT}s"
            else
                echo "[error] QEMU exited with status $rc" >&2
                exit $rc
            fi
        fi
    else
        "${args[@]}" "${QEMU_EXTRA[@]}"
    fi
}

case "$MODE" in
    lba)
        launch_qemu "LBA" 0
        ;;
    chs)
        launch_qemu "CHS" 1
        # Restore default build so subsequent invocations use LBA
        build_image 0
        ;;
    both)
        launch_qemu "LBA" 0
        launch_qemu "CHS" 1
        # Restore default build afterwards
        build_image 0
        ;;
    *)
        echo "[error] Internal error: unknown mode '$MODE'" >&2
        exit 1
        ;;
esac
