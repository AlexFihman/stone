#!/bin/bash
# Keep the two newest state files (hard links in DIR/keep) while PID lives.
# Usage: keeper.sh DIR PID
cd "$(dirname "$(readlink -f "$0")")" || exit 1
D=$1; P=$2; mkdir -p "$D/keep"
while kill -0 "$P" 2>/dev/null; do
  for f in "$D"/state_*.zst; do
    [ -e "$f" ] && [ ! -e "$D/keep/${f##*/}" ] && ln "$f" "$D/keep/" 2>/dev/null
  done
  ls "$D/keep" | grep -E '^state_[0-9]+\.zst$' | sort -t_ -k2 -n | head -n -2 | while read -r o; do rm -f "$D/keep/$o"; done
  sleep 5
done
