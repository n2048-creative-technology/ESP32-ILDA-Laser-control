#!/usr/bin/env bash
# Builds every PlatformIO firmware target in this repo and reports a
# pass/fail summary. Run this after any change under firmware/ before
# calling the change done - see CLAUDE.md.
#
# Usage: scripts/build_all.sh [extra pio run args, e.g. -e esp32dev]
set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failures=()

for project in "$repo_root"/firmware/*/; do
  [ -f "$project/platformio.ini" ] || continue
  name="$(basename "$project")"
  echo "==> Building firmware/$name"
  if (cd "$project" && pio run "$@"); then
    echo "==> firmware/$name: OK"
  else
    echo "==> firmware/$name: FAILED"
    failures+=("$name")
  fi
  echo
done

if [ "${#failures[@]}" -ne 0 ]; then
  echo "Build failed for: ${failures[*]}"
  exit 1
fi

echo "All firmware targets built successfully."
