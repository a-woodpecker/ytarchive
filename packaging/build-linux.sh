#!/usr/bin/env bash
#
# Build YT Archive on Debian, Ubuntu and derivatives.
#
#   ./packaging/build-linux.sh                 configure + build
#   ./packaging/build-linux.sh --run           build, then launch it
#   ./packaging/build-linux.sh --clean         throw the build tree away first
#   ./packaging/build-linux.sh --debug         debug build with symbols
#   ./packaging/build-linux.sh --install-deps  apt-get the build requirements
#   ./packaging/build-linux.sh -j4             limit parallel jobs
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname -- "$SCRIPT_DIR")"

BUILD_TYPE="Release"
BUILD_DIR="$PROJECT_ROOT/build/linux"
JOBS="$(nproc 2>/dev/null || echo 2)"
DO_CLEAN=0
DO_RUN=0
DO_INSTALL_DEPS=0

# Only colourise when stdout is a terminal, so piping to a file stays readable.
if [ -t 1 ]; then
    BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'
    YELLOW=$'\033[33m'; DIM=$'\033[2m'; RESET=$'\033[0m'
else
    BOLD=""; RED=""; GREEN=""; YELLOW=""; DIM=""; RESET=""
fi

info()  { printf '%s==>%s %s\n' "$BOLD" "$RESET" "$*"; }
warn()  { printf '%s warn%s %s\n' "$YELLOW" "$RESET" "$*"; }
die()   { printf '%serror%s %s\n' "$RED" "$RESET" "$*" >&2; exit 1; }
ok()    { printf '%s  ok%s %s\n' "$GREEN" "$RESET" "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)        DO_CLEAN=1 ;;
        --run)          DO_RUN=1 ;;
        --debug)        BUILD_TYPE="Debug"; BUILD_DIR="$PROJECT_ROOT/build/linux-debug" ;;
        --install-deps) DO_INSTALL_DEPS=1 ;;
        -j*)            JOBS="${1#-j}" ;;
        -j)             shift; JOBS="${1:-1}" ;;
        -h|--help)      sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
        *)              die "unknown option: $1  (try --help)" ;;
    esac
    shift
done

# --------------------------------------------------------------- packages --

# Package -> a file it provides, so the check works without dpkg-query parsing.
BUILD_PACKAGES=(
    "qt6-base-dev:/usr/lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake"
    "cmake:/usr/bin/cmake"
    "g++:/usr/bin/g++"
)
# Loaded at runtime as plugins: absent, they fail in confusing ways rather than
# at build time, so they are worth checking here.
RUNTIME_PACKAGES=(
    "libqt6sql6-sqlite:the catalog database cannot open without it"
    "qt6-wayland:needed for native Wayland; without it Qt falls back to XWayland"
)

if [ "$DO_INSTALL_DEPS" = "1" ]; then
    info "Installing build requirements"
    SUDO=""
    [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    $SUDO apt-get update
    $SUDO apt-get install -y \
        qt6-base-dev libqt6sql6-sqlite qt6-wayland \
        cmake ninja-build g++ ffmpeg
    ok "build requirements installed"
fi

info "Checking build requirements"
MISSING=()
for entry in "${BUILD_PACKAGES[@]}"; do
    pkg="${entry%%:*}"; probe="${entry#*:}"
    # Fall back to a glob for non-amd64 multiarch paths.
    if [ -e "$probe" ] || compgen -G "${probe/x86_64-linux-gnu/*}" > /dev/null; then
        ok "$pkg"
    else
        MISSING+=("$pkg")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    printf '\n'
    die "missing: ${MISSING[*]}
       Install them with:
         ./packaging/build-linux.sh --install-deps
       or manually:
         sudo apt install ${MISSING[*]}"
fi

# ---------------------------------------------------------------- toolset --

GENERATOR=""
if command -v ninja > /dev/null 2>&1; then
    GENERATOR="-G Ninja"
    ok "ninja"
else
    warn "ninja not found, falling back to make (slower incremental builds)"
    warn "  sudo apt install ninja-build"
fi

# ------------------------------------------------------------------ build --

if [ "$DO_CLEAN" = "1" ] && [ -d "$BUILD_DIR" ]; then
    info "Removing $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

info "Configuring ($BUILD_TYPE)"
# shellcheck disable=SC2086
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" $GENERATOR \
      -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

info "Building with $JOBS job(s)"
cmake --build "$BUILD_DIR" -j "$JOBS"

BINARY="$BUILD_DIR/ytarchive"
[ -x "$BINARY" ] || die "the build finished but $BINARY was not produced"

# ------------------------------------------------- runtime sanity checks ---

printf '\n'
info "Checking runtime requirements"

QT_PLUGIN_ROOT="$(dirname "$(find /usr/lib -name 'libqsqlite.so' -path '*sqldrivers*' 2>/dev/null | head -1)" 2>/dev/null || true)"
if [ -n "$QT_PLUGIN_ROOT" ]; then
    ok "libqt6sql6-sqlite"
else
    warn "libqt6sql6-sqlite is missing: ${RUNTIME_PACKAGES[0]#*:}"
fi

if find /usr/lib -name 'libqwayland-*.so' 2>/dev/null | grep -q .; then
    ok "qt6-wayland"
else
    warn "qt6-wayland is missing: ${RUNTIME_PACKAGES[1]#*:}"
fi

if command -v ffmpeg > /dev/null 2>&1; then
    ok "ffmpeg  $(ffmpeg -version 2>/dev/null | head -1 | cut -d' ' -f3)"
else
    warn "ffmpeg not found. Downloads will run to completion and then fail at the"
    warn "  merge step.  sudo apt install ffmpeg"
fi

if command -v yt-dlp > /dev/null 2>&1; then
    YTDLP_VERSION="$(yt-dlp --version 2>/dev/null || echo unknown)"
    ok "yt-dlp  $YTDLP_VERSION"
    # The apt package lags, and a stale yt-dlp fails against the service in ways
    # that look like bugs in this program.
    if readlink -f "$(command -v yt-dlp)" | grep -q '^/usr/bin/'; then
        warn "that yt-dlp came from apt, which lags upstream. Prefer:"
        warn "  sudo apt remove yt-dlp && pipx install yt-dlp"
    fi
else
    warn "yt-dlp not found. Nothing can be listed or downloaded without it."
    warn "  sudo apt install pipx && pipx install yt-dlp"
fi

printf '\n'
info "Built ${BOLD}$BINARY${RESET}"
printf '%s       %s%s\n' "$DIM" "$(du -h "$BINARY" | cut -f1)" "$RESET"

if [ "$DO_RUN" = "1" ]; then
    info "Launching"
    exec "$BINARY"
fi
