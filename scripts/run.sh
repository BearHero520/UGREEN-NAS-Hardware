#!/usr/bin/env sh
# Build ugreenctl in the source tree when needed, then execute it.
#
# Examples:
#   ./scripts/run.sh models
#   ./scripts/run.sh info
#   ./scripts/run.sh --apply fan set cpu 120

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
BIN="$BUILD_DIR/ugreenctl"
MODELS="$BUILD_DIR/models"

if [ "$(uname -s)" != "Linux" ]; then
    printf '%s\n' "error: run this script on the Linux NAS, not on Windows or macOS." >&2
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1 || ! command -v cc >/dev/null 2>&1; then
    printf '%s\n' "error: CMake and a C compiler are required; run ./scripts/install.sh --install-deps." >&2
    exit 1
fi

if [ ! -x "$BIN" ] || [ ! -d "$MODELS" ]; then
    cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
fi
cmake --build "$BUILD_DIR" --parallel

needs_root=0
for argument in "$@"; do
    if [ "$argument" = "--apply" ]; then
        needs_root=1
        break
    fi
done

if [ "$needs_root" -eq 1 ] && [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        exec sudo "$BIN" --plugin-dir "$MODELS" "$@"
    fi
    printf '%s\n' "error: a hardware write requires root; rerun as root." >&2
    exit 1
fi

exec "$BIN" --plugin-dir "$MODELS" "$@"
