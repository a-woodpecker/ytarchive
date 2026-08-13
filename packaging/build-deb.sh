#!/usr/bin/env bash
#
# Build a .deb of YT Archive and, optionally, install it.
#
#   ./packaging/build-deb.sh                  build the package
#   ./packaging/build-deb.sh --install        build, then install it with apt
#   ./packaging/build-deb.sh --version 0.2.0  override the version
#   ./packaging/build-deb.sh --maintainer "Name <you@example.com>"
#
# The result lands in packaging/dist/.
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname -- "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build/deb"
STAGE_DIR="$SCRIPT_DIR/stage"
DIST_DIR="$SCRIPT_DIR/dist"

PKG_NAME="ytarchive"
VERSION=""
MAINTAINER="a-woodpecker <a-woodpecker@users.noreply.github.com>"
DO_INSTALL=0
JOBS="$(nproc 2>/dev/null || echo 2)"

if [ -t 1 ]; then
    BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'
    YELLOW=$'\033[33m'; RESET=$'\033[0m'
else
    BOLD=""; RED=""; GREEN=""; YELLOW=""; RESET=""
fi
info() { printf '%s==>%s %s\n' "$BOLD" "$RESET" "$*"; }
ok()   { printf '%s  ok%s %s\n' "$GREEN" "$RESET" "$*"; }
warn() { printf '%s warn%s %s\n' "$YELLOW" "$RESET" "$*"; }
die()  { printf '%serror%s %s\n' "$RED" "$RESET" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --install)    DO_INSTALL=1 ;;
        --version)    shift; VERSION="${1:-}" ;;
        --maintainer) shift; MAINTAINER="${1:-}" ;;
        -j*)          JOBS="${1#-j}" ;;
        -h|--help)    sed -n '2,11p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
        *)            die "unknown option: $1  (try --help)" ;;
    esac
    shift
done

for tool in dpkg-deb dpkg-shlibdeps dpkg-architecture cmake; do
    command -v "$tool" > /dev/null 2>&1 || \
        die "$tool not found. Install the build tooling:
       sudo apt install dpkg-dev cmake"
done

# The version comes from CMakeLists.txt so the package, the binary and the
# in-app update check can never disagree.
if [ -z "$VERSION" ]; then
    VERSION="$(sed -n 's/^project(YtArchive VERSION \([0-9.]*\).*/\1/p' "$PROJECT_ROOT/CMakeLists.txt")"
    [ -n "$VERSION" ] || die "could not read the version from CMakeLists.txt"
fi
ARCH="$(dpkg-architecture -qDEB_HOST_ARCH)"

# The package is only valid for the distribution it is built on: Qt's runtime
# package names differ (Ubuntu carries a t64 suffix on amd64 after the 64-bit
# time_t transition, Debian does not), so the codename goes into the version
# and filename to keep two builds distinguishable.
DISTRO_ID=""
CODENAME=""
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    DISTRO_ID="$(. /etc/os-release && printf '%s' "${ID:-}")"
    CODENAME="$(. /etc/os-release && printf '%s' "${VERSION_CODENAME:-}")"
fi
PKG_VERSION="$VERSION${CODENAME:+~$CODENAME}"

info "Packaging $PKG_NAME $PKG_VERSION ($ARCH)"
[ -n "$CODENAME" ] && info "Target: ${DISTRO_ID:-unknown} $CODENAME (this machine)"

# ----------------------------------------------------------------- build ---

info "Building"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      $(command -v ninja > /dev/null 2>&1 && echo "-G Ninja") > /dev/null
cmake --build "$BUILD_DIR" -j "$JOBS"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/DEBIAN"
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR" > /dev/null
# Debian packages ship unstripped binaries only for -dbg variants.
strip --strip-unneeded "$STAGE_DIR/usr/bin/$PKG_NAME" 2>/dev/null || true
ok "staged $(find "$STAGE_DIR/usr" -type f | wc -l) files"

# ---------------------------------------------------- computed dependencies -

# Qt's runtime package names differ between distributions (Ubuntu carries a
# t64 suffix after the time_t transition, Debian does not), so the shared
# library dependencies are derived from the binary rather than hardcoded.
info "Resolving library dependencies"
SHLIB_WORK="$(mktemp -d)"
trap 'rm -rf "$SHLIB_WORK"' EXIT
mkdir -p "$SHLIB_WORK/debian"
cat > "$SHLIB_WORK/debian/control" <<CONTROL
Source: $PKG_NAME
Section: video
Priority: optional
Maintainer: $MAINTAINER

Package: $PKG_NAME
Architecture: $ARCH
Description: placeholder used only for dependency resolution
CONTROL

SHLIB_DEPS="$(cd "$SHLIB_WORK" && dpkg-shlibdeps -O --ignore-missing-info \
                 "$STAGE_DIR/usr/bin/$PKG_NAME" 2>/dev/null \
                 | sed 's/^shlibs:Depends=//')"
[ -n "$SHLIB_DEPS" ] || die "dpkg-shlibdeps produced no dependencies"
ok "$(echo "$SHLIB_DEPS" | tr ',' '\n' | wc -l) library dependencies resolved"

# Loaded with dlopen at runtime, so no ELF reference exists for shlibdeps to
# find. Without the SQLite driver the catalog cannot open at all.
EXTRA_DEPS="libqt6sql6-sqlite, ffmpeg"
ALL_DEPS="$SHLIB_DEPS, $EXTRA_DEPS"

INSTALLED_SIZE="$(du -ks "$STAGE_DIR/usr" | cut -f1)"

# ---------------------------------------------------------------- control --

cat > "$STAGE_DIR/DEBIAN/control" <<CONTROL
Package: $PKG_NAME
Version: $PKG_VERSION
Section: video
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER
Installed-Size: $INSTALLED_SIZE
Depends: $ALL_DEPS
Suggests: yt-dlp
Homepage: https://github.com/a-woodpecker/ytarchive
Description: Archive video channels for preservation
 A desktop application for keeping a local, dated archive of video channels.
 It lists a channel's uploads, lets you choose what to keep, and downloads
 them into a per-channel folder tree indexed by a SQLite catalog.
 .
 Every file is stamped with its upload date rather than its download date, and
 metadata, thumbnails, descriptions and subtitles are saved alongside the media
 so the archive stays meaningful without this program.
 .
 Downloading is performed by yt-dlp, which is deliberately only suggested
 rather than depended on: the packaged version lags well behind upstream, and a
 stale yt-dlp fails against the service in ways that look like faults in this
 program. Install a current one with "pipx install yt-dlp", or point
 Preferences at whichever binary you prefer.
CONTROL

# Refresh the desktop and icon caches when the tools are present. Both are
# no-ops on systems that do not use them.
cat > "$STAGE_DIR/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
    if command -v update-desktop-database > /dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v gtk-update-icon-cache > /dev/null 2>&1; then
        gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
POSTINST

cat > "$STAGE_DIR/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
    if command -v update-desktop-database > /dev/null 2>&1; then
        update-desktop-database -q /usr/share/applications || true
    fi
    if command -v gtk-update-icon-cache > /dev/null 2>&1; then
        gtk-update-icon-cache -q -f /usr/share/icons/hicolor || true
    fi
fi
exit 0
POSTRM

chmod 0755 "$STAGE_DIR/DEBIAN/postinst" "$STAGE_DIR/DEBIAN/postrm"

# ------------------------------------------------------------------ build --

mkdir -p "$DIST_DIR"
DEB_PATH="$DIST_DIR/${PKG_NAME}_${PKG_VERSION}_${ARCH}.deb"
info "Building the package"
# --root-owner-group forces root:root ownership without needing fakeroot.
dpkg-deb --root-owner-group --build "$STAGE_DIR" "$DEB_PATH" > /dev/null
ok "$DEB_PATH"

printf '\n'
dpkg-deb --info "$DEB_PATH" | sed 's/^/    /'
printf '\n'
info "Contents"
dpkg-deb --contents "$DEB_PATH" | awk '{print "    " $6, $7, $8}'

if [ "$DO_INSTALL" = "1" ]; then
    printf '\n'
    info "Installing"
    SUDO=""
    [ "$(id -u)" -ne 0 ] && SUDO="sudo"
    # apt, not dpkg -i, so that dependencies are pulled in automatically.
    $SUDO apt-get install -y "$DEB_PATH"
    ok "installed. Launch it from your application menu, or run: $PKG_NAME"
else
    printf '\n'
    info "Install it with:"
    printf '    sudo apt install %s\n' "$DEB_PATH"
fi

printf '\n'
warn "This package is built for ${DISTRO_ID:-this system} ${CODENAME:-} on $ARCH."
warn "Qt's library packages are named differently on Debian and Ubuntu, so run"
warn "this script again on any other distribution rather than copying the .deb."
