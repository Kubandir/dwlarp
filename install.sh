#!/bin/sh
# dwlarp installer. Supported: Void, Arch, Debian/Ubuntu.
#
# Usage:
#     ./install.sh                full install (greeter, fonts, configs, ly)
#     ./install.sh --rebuild      rebuild C projects only (after editing config.h)
#     ./install.sh --skip-deps    skip distro packages
#     ./install.sh -y             non-interactive
#     ./install.sh -h             help
#
# Curl-pipeable:
#     curl -fsSL https://raw.githubusercontent.com/Kubandir/dwlarp/main/install.sh | sh

set -eu

REBUILD=0; SKIP_DEPS=0; FORCE=${FORCE:-0}
for a in "$@"; do
	case "$a" in
		-r|--rebuild)         REBUILD=1 ;;
		-d|--skip-deps)       SKIP_DEPS=1 ;;
		-f|--force)           FORCE=1 ;;
		-y|--noconfirm)       ;;   # accepted for compatibility; install is non-interactive
		-h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) printf 'unknown flag: %s\n' "$a" >&2; exit 2 ;;
	esac
done
export FORCE

say()  { printf '\n>>> %s\n' "$*"; }
warn() { printf '!!! %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

if [ "$(id -u)" -eq 0 ]; then
	SUDO=
else
	have sudo || die "need sudo or root"
	SUDO=sudo
fi

# ---- locate source (local checkout next to script, else clone) ----
SCRIPT_DIR=$(cd "$(dirname "$0")" 2>/dev/null && pwd || echo "")
if [ -n "$SCRIPT_DIR" ] && [ -f "$SCRIPT_DIR/dwl/dwl.c" ]; then
	SRC=$SCRIPT_DIR
else
	SRC=$(mktemp -d)/dwlarp
	say "cloning source"
	git clone --depth=1 https://github.com/Kubandir/dwlarp.git "$SRC"
fi

# ---- distro detection ----
. /etc/os-release
case "${ID:-}${ID_LIKE:-}" in
	*void*)            DISTRO=void ;;
	*arch*|*manjaro*)  DISTRO=arch ;;
	*debian*|*ubuntu*) DISTRO=debian ;;
	*) die "unsupported distro: ${PRETTY_NAME:-unknown}" ;;
esac

have_pipewire() {
	have pipewire || { have pactl && pactl info 2>/dev/null | grep -qi pipewire; }
}

# ---- distro packages ----
PKGS_void="
	base-devel git pkg-config meson ninja
	wayland-devel wayland-protocols libxkbcommon-devel libinput-devel
	pixman-devel libxcb-devel xcb-util-wm-devel libdrm-devel
	libseat-devel hwids libdisplay-info-devel pulseaudio-devel
	pango-devel cairo-devel glib-devel
	elogind
	foot swaybg mako brightnessctl playerctl swaylock
	grim slurp wl-clipboard wlr-randr ImageMagick
	xdg-desktop-portal xdg-desktop-portal-gtk
	adwaita-icon-theme hicolor-icon-theme
	xorg-server-xwayland fontconfig
	wlsunset bluetuith impala pulsemixer
	wireguard-tools jq
	curl unzip"
PKGS_arch="
	base-devel git pkgconf meson ninja
	wayland wayland-protocols libxkbcommon libinput pixman
	libxcb xcb-util-wm libdrm seatd hwdata libdisplay-info libpulse
	pango cairo glib2
	foot swaybg mako brightnessctl playerctl swaylock
	grim slurp wl-clipboard wlr-randr imagemagick
	xdg-desktop-portal xdg-desktop-portal-gtk
	adwaita-icon-theme hicolor-icon-theme
	xorg-xwayland fontconfig
	wlsunset impala pulsemixer bluez-utils
	wireguard-tools jq
	curl unzip"
PKGS_debian="
	build-essential git pkg-config meson ninja-build
	libwayland-dev wayland-protocols libwayland-bin
	libxkbcommon-dev libinput-dev libpixman-1-dev
	libxcb1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-ewmh-dev libxcb-res0-dev
	libxcb-composite0-dev libxcb-xfixes0-dev libxcb-render0-dev
	libdrm-dev libseat-dev libudev-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
	libdisplay-info-dev hwdata libpulse-dev
	libpango1.0-dev libcairo2-dev libglib2.0-dev
	foot swaybg mako-notifier brightnessctl playerctl swaylock
	grim slurp wl-clipboard wlr-randr imagemagick
	xdg-desktop-portal xdg-desktop-portal-gtk xwayland
	adwaita-icon-theme hicolor-icon-theme
	wlsunset pulsemixer bluez
	wireguard-tools jq
	fontconfig curl unzip ca-certificates"
PIPEWIRE_void="pipewire wireplumber"
PIPEWIRE_arch="pipewire pipewire-pulse wireplumber"
PIPEWIRE_debian="pipewire pipewire-pulse wireplumber"

install_deps() {
	eval "pkgs=\$PKGS_$DISTRO"
	have_pipewire || eval "pkgs=\"\$pkgs \$PIPEWIRE_$DISTRO\""
	case "$DISTRO" in
		void)
			$SUDO xbps-install -Sy >/dev/null || warn "repo sync failed"
			missing=
			for p in $pkgs; do
				xbps-query -p pkgver "$p" >/dev/null 2>&1 && continue
				if xbps-query -R "$p" >/dev/null 2>&1; then
					missing="$missing $p"
				else
					warn "void: package '$p' not in repos — skipping"
				fi
			done
			[ -n "$missing" ] && { $SUDO xbps-install -y $missing || die "xbps-install failed"; }
			$SUDO xbps-install -y wlroots0.19 wlroots0.19-devel 2>/dev/null \
				|| $SUDO xbps-install -y wlroots-devel 2>/dev/null \
				|| warn "wlroots not in repos — will build from source" ;;
		arch)
			$SUDO pacman -S --needed --noconfirm $pkgs || die "pacman failed"
			$SUDO pacman -S --needed --noconfirm wlroots0.19 2>/dev/null \
				|| warn "wlroots0.19 not in repos — will build from source" ;;
		debian)
			$SUDO apt-get update
			$SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y \
				--no-install-recommends $pkgs || die "apt-get failed"
			if apt-cache show libwlroots-0.19-dev >/dev/null 2>&1; then
				$SUDO apt-get install -y libwlroots-0.19-dev || true
			fi ;;
	esac
}

# ---- wlroots 0.19 fallback ----
WLR_PREFIX=/usr/local
ensure_wlroots() {
	if pkg-config --exists wlroots-0.19; then
		say "wlroots 0.19 found ($(pkg-config --modversion wlroots-0.19))"
		return
	fi
	say "building wlroots 0.19 from source"
	tmp=$(mktemp -d)
	git clone --depth=1 --branch 0.19 \
		https://gitlab.freedesktop.org/wlroots/wlroots.git "$tmp/wlroots"
	(
		cd "$tmp/wlroots"
		meson setup build --prefix="$WLR_PREFIX" --buildtype=release \
			-Dexamples=false -Dxwayland=enabled
		ninja -C build
		$SUDO ninja -C build install
	)
	$SUDO ldconfig
	export PKG_CONFIG_PATH="$WLR_PREFIX/lib/pkgconfig:$WLR_PREFIX/lib64/pkgconfig:$WLR_PREFIX/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
	pkg-config --exists wlroots-0.19 || die "wlroots built but pkg-config can't find it"
}

# ---- dmenu-wayland (no upstream package; two patches required) ----
install_dmenu_wl() {
	have dmenu-wl && { say "dmenu-wl already installed"; return; }
	say "building dmenu-wayland"
	tmp=$(mktemp -d)
	git clone --depth=1 https://github.com/nyyManni/dmenu-wayland.git \
		"$tmp/dmenu-wayland"
	(
		cd "$tmp/dmenu-wayland"
		# overlap dwlb instead of stacking under it
		sed -i 's|zwlr_layer_surface_v1_set_anchor(panel->surface.layer_surface,|zwlr_layer_surface_v1_set_exclusive_zone(panel->surface.layer_surface, -1);\n\tzwlr_layer_surface_v1_set_anchor(panel->surface.layer_surface,|' draw.c
		# keep selection visible past visible width
		sed -i 's|for (item = matches; item; item = item->right) {$|for (item = sel ? sel : matches; item; item = item->right) {|' dmenu.c
		sed -i 's|if (leftmost != matches) {|if (sel \&\& sel != matches) {|' dmenu.c
		meson setup build --buildtype=release
		ninja -C build
		$SUDO ninja -C build install
	)
	have dmenu-wl || die "dmenu-wl built but not on PATH"
}

# ---- C projects ----
# Incremental: rely on make's mtime tracking. Pass FORCE=1 (or `--rebuild --force`)
# to nuke artifacts and recompile from scratch.
build() {
	[ "${FORCE:-0}" = 1 ] && make -C "$1" clean >/dev/null 2>&1 || true
	make -C "$1"
}
build_all() {
	say "building dwl";             build "$SRC/dwl";             $SUDO make -C "$SRC/dwl"             PREFIX=/usr install
	say "building dwlb";            build "$SRC/dwlb";            $SUDO make -C "$SRC/dwlb"            PREFIX=/usr install
	say "building dwlb-status";     build "$SRC/dwlb-status";     make -C "$SRC/dwlb-status"           PREFIX="$HOME/.local" install
	say "building dwlb-leftstatus"; build "$SRC/dwlb-leftstatus"; make -C "$SRC/dwlb-leftstatus"       PREFIX="$HOME/.local" install
	say "building ws-hud";          build "$SRC/ws-hud";          make -C "$SRC/ws-hud"                PREFIX="$HOME/.local" install
}

# ---- scripts (single source of truth, used by full install AND --rebuild) ----
SCRIPTS="dwl-autostart dwl-wallpaper dwl-autolayout dwl-watch-outputs screenshot-area dmenu-launcher dwl-osd ws-pomodoro ws-powermenu ws-hud-lidlock"
install_scripts() {
	mkdir -p "$HOME/.local/bin"
	for s in $SCRIPTS; do
		install -Dm755 "$SRC/scripts/$s" "$HOME/.local/bin/$s"
	done
	rm -f "$HOME/.local/bin/bemenu-desktop"  # legacy launcher
	# /usr/bin so ly's PATH (which lists /usr/bin before /usr/local/bin) picks
	# up the dbus-wrapping launcher.
	$SUDO install -Dm755 "$SRC/scripts/dwlarp"         /usr/bin/dwlarp
	$SUDO install -Dm644 "$SRC/desktop/dwlarp.desktop" /usr/share/wayland-sessions/dwlarp.desktop
	# Drop legacy entries from earlier installs.
	$SUDO rm -f /usr/local/bin/dwl-session /usr/bin/dwl-session \
		/usr/share/wayland-sessions/dwl.desktop

	# Mullvad helpers — root-owned in /usr/local/bin so sudoers whitelists by
	# absolute path. Bootstrap once: sudo mullvad-wg-setup <ACCOUNT_NUMBER>.
	$SUDO install -Dm755 "$SRC/scripts/mullvad-wg-setup"  /usr/local/bin/mullvad-wg-setup
	$SUDO install -Dm755 "$SRC/scripts/mullvad-vpn"       /usr/local/bin/mullvad-vpn
	$SUDO install -Dm440 "$SRC/assets/sudoers-ws-mullvad" /etc/sudoers.d/ws-mullvad

	# elogind sleep hook: re-unblock wifi+bluetooth on resume; lid-close →
	# hibernate skips /etc/zzz.d/. Background retries catch delayed firmware
	# rfkill events (hp_wmi).
	$SUDO install -Dm755 "$SRC/assets/elogind-rfkill-unblock" \
		/usr/libexec/elogind/system-sleep/99-rfkill-unblock
}

# ---- config.h macro readers (for swaylock template) ----
read_str() { awk -v k="$1" '$1=="#define" && $2==k { sub(/^[^"]*"/,""); sub(/"[^"]*$/,""); print; exit }' "$SRC/config.h"; }
read_num() { awk -v k="$1" '$1=="#define" && $2==k { print $3; exit }' "$SRC/config.h"; }

# ---- configs / wallpaper ----
seed_configs() {
	cfg="${XDG_CONFIG_HOME:-$HOME/.config}"
	[ -f "$cfg/dwl/layout"    ] || { mkdir -p "$cfg/dwl"; echo above > "$cfg/dwl/layout"; }
	[ -f "$cfg/foot/foot.ini" ] || install -Dm644 "$SRC/assets/foot.ini"   "$cfg/foot/foot.ini"
	[ -f "$cfg/mako/config"   ] || install -Dm644 "$SRC/assets/mako.config" "$cfg/mako/config"

	# xdg-desktop-portal: tell it to route FileChooser/AppChooser/Settings to
	# the gtk backend (file pickers) and screencast/screenshot to wlr.
	$SUDO install -Dm644 "$SRC/assets/dwl-portals.conf" \
		/usr/share/xdg-desktop-portal/dwl-portals.conf

	if [ ! -f "$cfg/swaylock/config" ]; then
		mkdir -p "$cfg/swaylock"
		sed -e "s|@WS_LOCK_SCREEN_HEX@|$(read_str WS_LOCK_SCREEN_HEX)|g" \
		    -e "s|@WS_LOCK_RING_HEX@|$(read_str WS_LOCK_RING_HEX)|g"     \
		    -e "s|@WS_LOCK_TEXT_HEX@|$(read_str WS_LOCK_TEXT_HEX)|g"     \
		    -e "s|@WS_LOCK_WRONG_HEX@|$(read_str WS_LOCK_WRONG_HEX)|g"   \
		    -e "s|@WS_LOCK_FONT@|$(read_str WS_LOCK_FONT)|g"             \
		    -e "s|@WS_LOCK_FONT_SIZE@|$(read_num WS_LOCK_FONT_SIZE)|g"   \
		    "$SRC/assets/swaylock.config.in" > "$cfg/swaylock/config"
	fi

	wall=$(read_str WS_WALLPAPER); [ -z "$wall" ] && wall=assets/wallpaper.png
	case "$wall" in /*) ;; *) wall="$SRC/$wall" ;; esac
	[ -f "$wall" ] && install -Dm644 "$wall" "${XDG_DATA_HOME:-$HOME/.local/share}/dwl/wallpaper.png"
}

# ---- fonts ----
NERD_VER=v3.2.1
NERD_BASE="https://github.com/ryanoasis/nerd-fonts/releases/download/$NERD_VER"
font_present() { have fc-list && fc-list | grep -qi "$1"; }

fetch() {
	# usage: fetch URL OUT — try curl, then wget
	if have curl; then curl -fsSL -o "$2" "$1"
	elif have wget; then wget -q -O "$2" "$1"
	else return 1
	fi
}

install_nerd_fonts() {
	font_present "FiraCode Nerd Font" && return
	dest="$HOME/.local/share/fonts/NerdFonts/FiraCode"
	tmp=$(mktemp -d)
	say "downloading FiraCode Nerd Font"
	if fetch "$NERD_BASE/FiraCode.zip" "$tmp/FiraCode.zip"; then
		mkdir -p "$dest"
		unzip -oq "$tmp/FiraCode.zip" -d "$dest" -x "*.md" "*.txt" "LICENSE" || true
		fc-cache -f "$dest" >/dev/null 2>&1 || true
	else
		warn "FiraCode Nerd Font download failed — install curl or wget"
	fi
	rm -rf "$tmp"
}

install_fonts() {
	# Distro nerd-font packages are either too coarse (Void's `nerd-fonts`
	# meta-pkg pulls ~1 GB of fonts) or unreliable. Just fetch FiraCode.
	font_present "FiraCode Nerd Font" || install_nerd_fonts
	fc-cache -f >/dev/null 2>&1 || true
	font_present "FiraCode Nerd Font" \
		|| warn "FiraCode Nerd Font not detected — bar/foot may render boxes"
}

# ---- ly greeter ----
ly_enable() {
	[ -f /etc/ly/config.ini ] || $SUDO install -Dm644 "$SRC/assets/ly.config.ini" /etc/ly/config.ini
	if [ ! -s /etc/ly/save.ini ]; then
		u=${SUDO_USER:-$(id -un)}
		[ "$u" = root ] && u=$(awk -F: '$3>=1000 && $3<60000 && $7!~"nologin|false"{print $1; exit}' /etc/passwd)
		[ -n "$u" ] && printf 'user=%s\nsession_index=0\n' "$u" \
			| $SUDO tee /etc/ly/save.ini >/dev/null
	fi
	case "$DISTRO" in
		arch)   $SUDO systemctl enable ly ;;
		debian) have systemctl && $SUDO systemctl enable ly || true ;;
		void)   [ -L /var/service/ly ] || $SUDO ln -s /etc/sv/ly /var/service/ly ;;
	esac
	say "ly enabled — reboot to take effect"
}

ly_build_from_source() {
	# Void/Debian don't package ly; build it via zig.
	case "$DISTRO" in
		void)   $SUDO xbps-install -Sy zig pam-devel libxcb-devel >/dev/null 2>&1 || true ;;
		debian) $SUDO apt-get install -y --no-install-recommends zig libpam0g-dev libxcb1-dev 2>/dev/null || true ;;
	esac
	have zig || { warn "ly: zig unavailable on $DISTRO — skipping"; return 1; }
	tmp=$(mktemp -d)
	git clone --depth=1 https://github.com/fairyglade/ly.git "$tmp/ly" \
		|| { warn "ly: clone failed"; return 1; }
	(
		cd "$tmp/ly"
		zig build
		case "$DISTRO" in
			void) $SUDO zig build installexe -Dinit_system=runit ;;
			*)    $SUDO zig build installexe -Dinit_system=systemd ;;
		esac
	) || { warn "ly: build failed (zig version mismatch?)"; return 1; }
}

install_ly() {
	have ly && { say "ly already installed"; ly_enable; return; }
	case "$DISTRO" in
		arch) $SUDO pacman -S --needed --noconfirm ly 2>/dev/null && have ly && { ly_enable; return; } ;;
		void) $SUDO xbps-install -Sy ly 2>/dev/null && have ly && { ly_enable; return; } ;;
	esac
	say "building ly from source"
	ly_build_from_source && ly_enable
}

# ════════════════════════════════════════════════════════════════════════════

if [ "$REBUILD" -eq 1 ]; then
	say "rebuilding from $SRC"
	build_all
	install_scripts
	say "done — log out and back in"
	exit 0
fi

say "dwlarp installer — $DISTRO"
[ "$SKIP_DEPS" -eq 0 ] && { say "installing distro packages"; install_deps; }
say "ensuring wlroots 0.19"; ensure_wlroots
export PKG_CONFIG_PATH="$WLR_PREFIX/lib/pkgconfig:$WLR_PREFIX/lib64/pkgconfig:$WLR_PREFIX/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
install_dmenu_wl
build_all
say "seeding configs and scripts"; seed_configs; install_scripts
say "installing fonts"; install_fonts
say "installing ly greeter"; install_ly

case ":${PATH}:" in *":$HOME/.local/bin:"*) ;;
	*) warn "$HOME/.local/bin not in PATH — add it to your shell rc" ;;
esac

say "done — pick 'dwlarp' at the greeter (or run dwlarp from a TTY)"
say "rebuild after editing config.h:  ./install.sh --rebuild"
