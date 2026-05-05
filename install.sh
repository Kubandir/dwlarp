#!/usr/bin/env bash
# wayland-suckless installer for Void / Arch / Debian.
#
# Curl-pipeable:
#     curl -fsSL https://raw.githubusercontent.com/Kubandir/wayland-suckless/main/install.sh | bash
# Or from a local checkout:
#     ./install.sh
#
# Flags:
#     --skip-deps     Do not install distro packages (assume already present)
#     --skip-build    Do not (re)build wlroots even if 0.19 missing
#     --branch <ref>  Git branch/tag/sha to clone (default: main)
#     --local         Force using current working tree (skip clone)
#     -h | --help

set -euo pipefail

REPO_URL="https://github.com/Kubandir/wayland-suckless.git"
RAW_URL="https://raw.githubusercontent.com/Kubandir/wayland-suckless"
BRANCH="main"
SKIP_DEPS=0
SKIP_BUILD=0
FORCE_LOCAL=0

# ---------- argument parsing ----------
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-deps)  SKIP_DEPS=1 ;;
        --skip-build) SKIP_BUILD=1 ;;
        --branch)     BRANCH="$2"; shift ;;
        --local)      FORCE_LOCAL=1 ;;
        -h|--help)
            sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown flag: $1" >&2; exit 2 ;;
    esac
    shift
done

# ---------- pretty printing ----------
if [ -t 1 ]; then
    C_RST=$'\033[0m'; C_B=$'\033[1m'; C_G=$'\033[32m'; C_Y=$'\033[33m'; C_R=$'\033[31m'
else
    C_RST=; C_B=; C_G=; C_Y=; C_R=
fi
say()  { printf '%s==>%s %s%s%s\n' "$C_G" "$C_RST" "$C_B" "$*" "$C_RST"; }
warn() { printf '%s!! %s%s\n' "$C_Y" "$*" "$C_RST" >&2; }
die()  { printf '%sxx %s%s\n' "$C_R" "$*" "$C_RST" >&2; exit 1; }

# ---------- privilege helper ----------
if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
else
    if ! command -v sudo >/dev/null 2>&1; then
        die "sudo is required (or run as root)"
    fi
    SUDO="sudo"
fi

# ---------- distro detection ----------
detect_distro() {
    [ -r /etc/os-release ] || die "cannot read /etc/os-release"
    # shellcheck disable=SC1091
    . /etc/os-release
    case "${ID:-}${ID_LIKE:-}" in
        *void*)              echo void ;;
        *arch*|*manjaro*)    echo arch ;;
        *debian*|*ubuntu*)   echo debian ;;
        *)
            # Fallback by ID_LIKE
            case "${ID_LIKE:-}" in
                *arch*)   echo arch ;;
                *debian*) echo debian ;;
                *)        die "unsupported distro: ${PRETTY_NAME:-unknown}" ;;
            esac ;;
    esac
}
DISTRO=$(detect_distro)
say "detected distro: $DISTRO"

# ---------- pipewire detection ----------
# Treat PipeWire as "already managing audio" if either the binary exists or the
# user-service is running. On Debian/GNOME this is true out of the box; we must
# not yank or reinstall it.
have_pipewire() {
    command -v pipewire >/dev/null 2>&1 && return 0
    command -v pactl >/dev/null 2>&1 && pactl info 2>/dev/null | grep -qi pipewire && return 0
    return 1
}

# ---------- distro package installation ----------
install_deps_void() {
    local pkgs=(
        base-devel git pkg-config meson ninja
        wayland-devel wayland-protocols libxkbcommon-devel libinput-devel
        pixman-devel libxcb-devel xcb-util-wm-devel libdrm-devel
        libseat-devel hwdata libdisplay-info-devel
        libpulse-devel
        foot bemenu swaybg mako brightnessctl playerctl
        grim slurp wl-clipboard wlr-randr ImageMagick
        xdg-desktop-portal xdg-desktop-portal-gtk
        xauth xorg-server-xwayland
        nerd-fonts-firacode noto-fonts-ttf
    )
    if ! have_pipewire; then
        pkgs+=(pipewire wireplumber)
    else
        say "PipeWire already present — keeping it"
    fi
    say "installing Void packages"
    $SUDO xbps-install -Sy "${pkgs[@]}" || die "xbps-install failed"
    # wlroots: try common naming, ignore failure (ensure_wlroots will build if needed).
    $SUDO xbps-install -Sy wlroots0.19 wlroots0.19-devel 2>/dev/null \
        || $SUDO xbps-install -Sy wlroots-devel 2>/dev/null \
        || warn "no wlroots package matched — will build from source"
}

install_deps_arch() {
    local pkgs=(
        base-devel git pkgconf meson ninja
        wayland wayland-protocols libxkbcommon libinput pixman
        libxcb xcb-util-wm libdrm seatd hwdata libdisplay-info
        libpulse
        foot bemenu swaybg mako brightnessctl playerctl
        grim slurp wl-clipboard wlr-randr imagemagick swaylock
        xdg-desktop-portal xdg-desktop-portal-gtk
        xorg-xwayland
        ttf-firacode-nerd ttf-nerd-fonts-symbols
    )
    if ! have_pipewire; then
        pkgs+=(pipewire pipewire-pulse wireplumber)
    else
        say "PipeWire already present — keeping it"
    fi
    say "installing Arch packages"
    $SUDO pacman -S --needed --noconfirm "${pkgs[@]}" || die "pacman failed"
    # wlroots 0.19 ships as a side-by-side package on Arch.
    $SUDO pacman -S --needed --noconfirm wlroots0.19 2>/dev/null \
        || warn "wlroots0.19 not in repos — will build from source"
}

install_deps_debian() {
    local pkgs=(
        build-essential git pkg-config meson ninja-build
        libwayland-dev wayland-protocols libwayland-bin
        libxkbcommon-dev libinput-dev libpixman-1-dev
        libxcb1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-ewmh-dev libxcb-res0-dev
        libdrm-dev libseat-dev libudev-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
        libdisplay-info-dev hwdata
        libpulse-dev
        foot bemenu swaybg mako-notifier brightnessctl playerctl
        grim slurp wl-clipboard wlr-randr imagemagick swaylock
        xdg-desktop-portal xdg-desktop-portal-gtk
        xwayland
        fonts-firacode fonts-noto fonts-symbola
    )
    if ! have_pipewire; then
        pkgs+=(pipewire pipewire-pulse wireplumber)
    else
        say "PipeWire already present (Debian/GNOME default) — keeping it"
    fi
    say "installing Debian packages"
    $SUDO apt-get update
    $SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${pkgs[@]}" \
        || die "apt-get failed"
    # Trixie ships wlroots-0.18; dwl 0.8 needs 0.19. If sid/forky offers it, try.
    if ! pkg-config --exists wlroots-0.19; then
        if apt-cache show libwlroots-0.19-dev >/dev/null 2>&1; then
            $SUDO apt-get install -y libwlroots-0.19-dev || true
        fi
    fi
}

case "$DISTRO" in
    void)   [ "$SKIP_DEPS" -eq 0 ] && install_deps_void ;;
    arch)   [ "$SKIP_DEPS" -eq 0 ] && install_deps_arch ;;
    debian) [ "$SKIP_DEPS" -eq 0 ] && install_deps_debian ;;
esac

# ---------- ensure wlroots-0.19 ----------
WLR_PREFIX=/usr/local
ensure_wlroots() {
    if pkg-config --exists wlroots-0.19; then
        say "wlroots 0.19 found via pkg-config: $(pkg-config --modversion wlroots-0.19)"
        return
    fi
    [ "$SKIP_BUILD" -eq 1 ] && die "wlroots 0.19 missing and --skip-build set"

    say "wlroots 0.19 not packaged — building from source"
    local src; src=$(mktemp -d)
    git clone --depth=1 --branch 0.19 https://gitlab.freedesktop.org/wlroots/wlroots.git "$src/wlroots"
    (
        cd "$src/wlroots"
        meson setup build --prefix="$WLR_PREFIX" --buildtype=release \
              -Dexamples=false -Dxwayland=enabled
        ninja -C build
        $SUDO ninja -C build install
    )
    $SUDO ldconfig
    # Ensure pkg-config picks it up for subsequent steps.
    export PKG_CONFIG_PATH="$WLR_PREFIX/lib/pkgconfig:$WLR_PREFIX/lib64/pkgconfig:$WLR_PREFIX/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
    pkg-config --exists wlroots-0.19 || die "wlroots build succeeded but pkg-config still cannot find wlroots-0.19"
}
ensure_wlroots
export PKG_CONFIG_PATH="$WLR_PREFIX/lib/pkgconfig:$WLR_PREFIX/lib64/pkgconfig:$WLR_PREFIX/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"

# ---------- locate sources (local checkout vs. clone) ----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
if [ "$FORCE_LOCAL" -eq 1 ] || { [ -d "$SCRIPT_DIR/dwl" ] && [ -f "$SCRIPT_DIR/dwl/dwl.c" ]; }; then
    SRC="$SCRIPT_DIR"
    say "using local checkout: $SRC"
else
    SRC=$(mktemp -d)/wayland-suckless
    say "cloning $REPO_URL ($BRANCH) → $SRC"
    git clone --depth=1 --branch "$BRANCH" "$REPO_URL" "$SRC"
fi

# ---------- build & install dwl ----------
say "building dwl"
make -C "$SRC/dwl" clean >/dev/null 2>&1 || true
make -C "$SRC/dwl"
say "installing dwl → /usr/bin/dwl"
$SUDO make -C "$SRC/dwl" PREFIX=/usr install

# ---------- build & install dwlb ----------
say "building dwlb"
make -C "$SRC/dwlb" clean >/dev/null 2>&1 || true
make -C "$SRC/dwlb"
say "installing dwlb → /usr/bin/dwlb"
$SUDO make -C "$SRC/dwlb" PREFIX=/usr install

# ---------- build & install status feeders (user-local) ----------
mkdir -p "$HOME/.local/bin"
say "building dwlb-status"
make -C "$SRC/dwlb-status" clean >/dev/null 2>&1 || true
make -C "$SRC/dwlb-status"
make -C "$SRC/dwlb-status" PREFIX="$HOME/.local" install

say "building dwlb-leftstatus"
make -C "$SRC/dwlb-leftstatus" clean >/dev/null 2>&1 || true
make -C "$SRC/dwlb-leftstatus"
make -C "$SRC/dwlb-leftstatus" PREFIX="$HOME/.local" install

# ---------- install scripts, desktop entry, wallpaper ----------
say "installing user scripts → ~/.local/bin"
install -Dm755 "$SRC/scripts/dwl-autostart"      "$HOME/.local/bin/dwl-autostart"
install -Dm755 "$SRC/scripts/dwl-wallpaper"      "$HOME/.local/bin/dwl-wallpaper"
install -Dm755 "$SRC/scripts/dwl-autolayout"     "$HOME/.local/bin/dwl-autolayout"
install -Dm755 "$SRC/scripts/dwl-watch-outputs"  "$HOME/.local/bin/dwl-watch-outputs"
install -Dm755 "$SRC/scripts/screenshot-area"    "$HOME/.local/bin/screenshot-area"

# Seed default layout policy (one word: above|below|left|right). Only if the
# user has none — never clobber a manual choice.
LAYOUT_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/dwl/layout"
if [ ! -f "$LAYOUT_CFG" ]; then
    install -Dm644 /dev/null "$LAYOUT_CFG"
    printf 'above\n' > "$LAYOUT_CFG"
    say "seeded $LAYOUT_CFG (default: above — edit to below/left/right)"
fi

# swaylock theme matching dwl's teal palette. Only if user has none.
SWAYLOCK_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/swaylock/config"
if [ ! -f "$SWAYLOCK_CFG" ]; then
    say "seeding swaylock theme → $SWAYLOCK_CFG"
    install -Dm644 "$SRC/assets/swaylock.config" "$SWAYLOCK_CFG"
fi

say "installing dwl-session → /usr/local/bin (so greeters find it)"
$SUDO install -Dm755 "$SRC/scripts/dwl-session" /usr/local/bin/dwl-session

say "installing wayland session entry"
$SUDO install -Dm644 "$SRC/desktop/dwl.desktop" /usr/share/wayland-sessions/dwl.desktop

WALL_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/dwl"
say "installing wallpaper → $WALL_DIR/wallpaper.png"
install -Dm644 "$SRC/assets/wallpaper.png" "$WALL_DIR/wallpaper.png"

# ---------- PATH sanity ----------
case ":${PATH}:" in
    *":$HOME/.local/bin:"*) ;;
    *) warn "$HOME/.local/bin is not in PATH — add it to your shell rc" ;;
esac

cat <<EOF

${C_G}${C_B}done.${C_RST}

Log out, pick "dwl" at your display manager, and log back in. (Or run
${C_B}dwl-session${C_RST} from a TTY.)

Notes:
  • Wallpaper: $WALL_DIR/wallpaper.png  — replace to taste.
  • Config:    /usr/bin/dwl is built from $SRC/dwl/config.h. Re-run this script after edits.
  • Rebuild:   $SRC/install.sh --skip-deps
EOF
