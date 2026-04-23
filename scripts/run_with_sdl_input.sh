#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

CMAKE_PRESET="${CMAKE_PRESET:-unix-makefiles-debug}"
DEFAULT_ENGINE_BIN="./build/${CMAKE_PRESET}/game"
LEGACY_ENGINE_BIN="./game"
OLD_DEFAULT_ENGINE_BIN="./build/${CMAKE_PRESET}/game_engine_linux"
OLD_LEGACY_ENGINE_BIN="./game_engine_linux"
ENGINE_BIN="${ENGINE_BIN:-$DEFAULT_ENGINE_BIN}"
INPUT_SOURCE="${1:-resources/sdl_user_input.txt}"
OUTPUT_FILE="${2:-resources/sdl_out.txt}"
RUNTIME_INPUT_FILE="sdl_user_input.txt"
TIMEOUT_SECONDS="${TIMEOUT_SECONDS:-30}"

if [[ ! -f "$INPUT_SOURCE" ]]; then
  echo "input file not found: $INPUT_SOURCE" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT_FILE")"

if [[ "$ENGINE_BIN" == "$DEFAULT_ENGINE_BIN" && ! -x "$ENGINE_BIN" ]]; then
  if [[ -x "$LEGACY_ENGINE_BIN" ]]; then
    ENGINE_BIN="$LEGACY_ENGINE_BIN"
  elif [[ -x "$OLD_DEFAULT_ENGINE_BIN" ]]; then
    ENGINE_BIN="$OLD_DEFAULT_ENGINE_BIN"
  elif [[ -x "$OLD_LEGACY_ENGINE_BIN" ]]; then
    ENGINE_BIN="$OLD_LEGACY_ENGINE_BIN"
  fi
fi

if [[ ! -x "$ENGINE_BIN" && "$ENGINE_BIN" == "$DEFAULT_ENGINE_BIN" ]]; then
  cmake --preset "$CMAKE_PRESET"
  cmake --build --preset "$CMAKE_PRESET" -j"$(nproc 2>/dev/null || echo 4)"
fi

if [[ ! -x "$ENGINE_BIN" ]]; then
  echo "engine binary not found or not executable: $ENGINE_BIN" >&2
  exit 1
fi

backup_file=""
had_runtime_input=0
if [[ -f "$RUNTIME_INPUT_FILE" ]]; then
  backup_file="$(mktemp)"
  cp "$RUNTIME_INPUT_FILE" "$backup_file"
  had_runtime_input=1
fi

cleanup() {
  if [[ $had_runtime_input -eq 1 ]]; then
    cp "$backup_file" "$RUNTIME_INPUT_FILE"
    rm -f "$backup_file"
  else
    rm -f "$RUNTIME_INPUT_FILE"
  fi
}
trap cleanup EXIT

cp "$INPUT_SOURCE" "$RUNTIME_INPUT_FILE"

{
  /usr/bin/time -f $'[time]\nreal=%e s\nuser=%U s\nsys=%S s' \
    env AUTOGRADER=1 SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software \
    timeout "${TIMEOUT_SECONDS}s" "$ENGINE_BIN"
} > "$OUTPUT_FILE" 2>&1

if grep -q '^Command exited with non-zero status 124$' "$OUTPUT_FILE"; then
  echo "engine timed out after ${TIMEOUT_SECONDS}s; see $OUTPUT_FILE" >&2
  exit 124
fi

echo "wrote output to $OUTPUT_FILE"
