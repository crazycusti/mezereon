#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

QEMU_BIN="${QEMU_BIN:-qemu-system-i386}"
DISK_IMG="${DISK_IMG:-disk.img}"
TIMEOUT_SECS="${TIMEOUT_SECS:-10}"

if [[ ! -f "$DISK_IMG" ]]; then
  echo "[mem-sweep] Missing $DISK_IMG (run: make disk.img)" >&2
  exit 1
fi

if (( $# > 0 )); then
  SIZES=("$@")
else
  # "Small first" list: try to find the lowest size that still boots, then scale up.
  SIZES=(640K 704K 768K 896K 960K 1M 1536K 2M 3M 4M 6M 8M 12M 16M 24M 32M 48M 64M)
fi

extract_first() {
  local pattern="$1"
  rg -m1 -o "$pattern" || true
}

line_after_eq() {
  # Input: full line "X=VALUE (foo)" -> output "VALUE"
  sed -n 's/^[^=]*=\(.*\) (.*$/\1/p'
}

echo "| -m | E820 count | bios ext_kb | fallback | total physical | usable | kernel_end | paging | boot |"
echo "|---:|---:|---:|:---:|---:|---:|---:|:---:|:---:|"

for sz in "${SIZES[@]}"; do
  out="$(
    timeout "$TIMEOUT_SECS" \
      "$QEMU_BIN" -no-reboot -m "$sz" \
      -drive "file=$DISK_IMG,format=raw,if=ide" \
      -net none -display none -monitor none -serial stdio 2>/dev/null \
      | tr -d '\r' || true
  )"

  if [[ -z "$out" ]]; then
    echo "| $sz |  |  |  |  |  |  | no |"
    continue
  fi

  e820_count="$(printf '%s\n' "$out" | rg -m1 '^ count=' | extract_first '[0-9]+$')"
  bios_ext_kb="$(printf '%s\n' "$out" | rg -m1 'bios_mem:' | extract_first 'ext_kb=[0-9]+' | sed -n 's/^ext_kb=//p')"
  fallback="$(printf '%s\n' "$out" | rg -q '^E820 fallback:' && echo yes || echo no)"

  total_phys="$(printf '%s\n' "$out" | rg -m1 '^Memory: total physical=' | line_after_eq)"
  usable="$(printf '%s\n' "$out" | rg -m1 '^Memory: usable=' | line_after_eq)"
  kernel_end="$(printf '%s\n' "$out" | rg -m1 '^Memory: kernel-end=' | extract_first '0x[^ ]+' )"
  paging="$(printf '%s\n' "$out" | rg -m1 '^Paging: ' | sed -n 's/^Paging: //p')"

  boot="$(printf '%s\n' "$out" | rg -q '^mez> ' && echo yes || echo no)"

  echo "| $sz | ${e820_count:-} | ${bios_ext_kb:-} | $fallback | ${total_phys:-} | ${usable:-} | ${kernel_end:-} | ${paging:-} | $boot |"
done
