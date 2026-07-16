#!/usr/bin/env sh
# Build and install ugreenctl on a supported Linux NAS.
#
# Usage:
#   ./scripts/install.sh
#   ./scripts/install.sh --install-deps
#   PREFIX=/opt/ugreenctl ./scripts/install.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
PREFIX=${PREFIX:-/usr/local}
INSTALL_DEPS=0

usage() {
    cat <<'EOF'
Usage: scripts/install.sh [--install-deps] [--help]

Builds ugreenctl and installs it under PREFIX (default: /usr/local).

  --install-deps  Install CMake and a C compiler with the detected package
                  manager. This changes system packages.
EOF
}

for argument in "$@"; do
    case "$argument" in
        --install-deps) INSTALL_DEPS=1 ;;
        --help|-h) usage; exit 0 ;;
        *) printf '%s\n' "error: unknown option: $argument" >&2; usage >&2; exit 2 ;;
    esac
done

if [ "$(uname -s)" != "Linux" ]; then
    printf '%s\n' "error: ugreenctl can only be built on Linux." >&2
    exit 1
fi

machine=$(uname -m)
case "$machine" in
    x86_64|amd64|i?86) ;;
    *) printf '%s\n' "error: unsupported CPU architecture: $machine" >&2; exit 1 ;;
esac

run_as_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        printf '%s\n' "error: installation needs root; rerun this script as root." >&2
        exit 1
    fi
}

install_dependencies() {
    if [ "$(id -u)" -ne 0 ] && ! command -v sudo >/dev/null 2>&1; then
        printf '%s\n' "error: --install-deps needs root or sudo." >&2
        exit 1
    fi

    if command -v apt-get >/dev/null 2>&1; then
        run_as_root apt-get update
        run_as_root apt-get install -y build-essential cmake
    elif command -v dnf >/dev/null 2>&1; then
        run_as_root dnf install -y gcc make cmake
    elif command -v yum >/dev/null 2>&1; then
        run_as_root yum install -y gcc make cmake
    elif command -v pacman >/dev/null 2>&1; then
        run_as_root pacman -Sy --needed --noconfirm base-devel cmake
    elif command -v apk >/dev/null 2>&1; then
        run_as_root apk add build-base cmake
    else
        printf '%s\n' "error: no supported package manager; install a C compiler and CMake manually." >&2
        exit 1
    fi
}

if [ "$INSTALL_DEPS" -eq 1 ]; then
    install_dependencies
fi

if ! command -v cmake >/dev/null 2>&1; then
    printf '%s\n' "error: CMake is required. Rerun with --install-deps if supported." >&2
    exit 1
fi
if ! command -v cc >/dev/null 2>&1; then
    printf '%s\n' "error: a C compiler is required. Rerun with --install-deps if supported." >&2
    exit 1
fi

printf '%s\n' "Configuring ugreenctl..."
cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD_DIR" --parallel

printf '%s\n' "Installing to $PREFIX..."
run_as_root cmake --install "$BUILD_DIR"

cat <<EOF

Installed successfully.

Read-only status:
  sudo ugreenctl info

Preview a fan change (does not write):
  ugreenctl fan set cpu 120

Apply a fan change:
  sudo ugreenctl --apply fan set cpu 120
EOF
