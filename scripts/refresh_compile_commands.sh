#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PRESET="${1:-${CMAKE_PRESET:-unix-makefiles-debug}}"
BUILD_DIR="build/${PRESET}"
BUILD_DB="${BUILD_DIR}/compile_commands.json"
ROOT_DB="compile_commands.json"

cmake --preset "$PRESET"

if [[ ! -f "$BUILD_DB" ]]; then
  echo "compile_commands missing after configure: ${BUILD_DB}" >&2
  exit 1
fi

cp "$BUILD_DB" "$ROOT_DB"
echo "updated ${ROOT_DB} from ${BUILD_DB}"
