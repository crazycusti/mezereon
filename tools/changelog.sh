#!/usr/bin/env bash
set -euo pipefail

# Simple changelog appender. Usage:
#   tools/changelog.sh "short: message here"
# or via Makefile: make log MSG="short: message"

msg=${1:-}
if [[ -z "${msg}" ]]; then
  echo "Usage: $0 \"short: message\"" >&2
  exit 1
fi

ts=$(date +"%Y-%m-%d %H:%M:%S")
rev=""
if command -v git >/dev/null 2>&1; then
  if git rev-parse --git-dir >/dev/null 2>&1; then
    rev=" ($(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)@$(git rev-parse --short HEAD 2>/dev/null || true))"
  fi
fi

{
  # Ensure trailing newline in existing file for clean append
  if [[ -f CHANGELOG ]] && [[ -s CHANGELOG ]]; then
    tail -c1 CHANGELOG | grep -q $'\n' || echo >> CHANGELOG
  fi
  echo "${ts}${rev} - ${msg}" >> CHANGELOG
}
echo "Logged to CHANGELOG: ${msg}"
