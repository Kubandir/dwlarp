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

# ---- host selection ----
# Per-host config.h lives in hosts/<host>/. The repo-root config.h is
# regenerated from there on every install so the suckless single-header
# build model is preserved. Override the auto-pick with DWLARP_HOST=name.
HOST=${DWLARP_HOST:-$(uname -n 2>/dev/null | cut -d. -f1)}
HOST=${HOST:-void}
if [ ! -f "$SRC/hosts/$HOST/config.h" ]; then
	warn "hosts/$HOST/config.h missing — falling back to hosts/void"
	HOST=void
fi
[ -f "$SRC/hosts/$HOST/config.h" ] || die "no host config found (not even hosts/void/config.h)"
say "host = $HOST"
cp "$SRC/hosts/$HOST/config.h" "$SRC/config.h"

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
	pango-devel cairo-devel glib-devel ncurses-devel
	fcft-devel
	zsh
	elogind
	foot swaybg mako brightnessctl playerctl swaylock
	grim slurp wl-clipboard wlr-randr ImageMagick
	xdg-desktop-portal xdg-desktop-portal-gtk
	adwaita-icon-theme hicolor-icon-theme papirus-icon-theme papirus-folders
	sassc
	xorg-server-xwayland fontconfig
	wlsunset bluetuith impala pulsemixer
	wireguard-tools jq libnotify
	nftables e2fsprogs
	curl unzip"
PKGS_arch="
	base-devel git pkgconf meson ninja
	wayland wayland-protocols libxkbcommon libinput pixman
	libxcb xcb-util-wm libdrm seatd hwdata libdisplay-info libpulse
	pango cairo glib2 ncurses
	fcft
	zsh
	foot swaybg mako brightnessctl playerctl swaylock
	grim slurp wl-clipboard wlr-randr imagemagick
	xdg-desktop-portal xdg-desktop-portal-gtk
	adwaita-icon-theme hicolor-icon-theme papirus-icon-theme
	sassc
	xorg-xwayland fontconfig
	wlsunset impala pulsemixer bluez-utils
	wireguard-tools jq libnotify
	nftables e2fsprogs
	curl unzip"
PKGS_debian="
	build-essential git pkg-config meson ninja-build
	libwayland-dev wayland-protocols libwayland-bin
	libxkbcommon-dev libinput-dev libpixman-1-dev
	libxcb1-dev libxcb-icccm4-dev libxcb-render-util0-dev libxcb-ewmh-dev libxcb-res0-dev
	libxcb-composite0-dev libxcb-xfixes0-dev libxcb-render0-dev
	libdrm-dev libseat-dev libudev-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
	libdisplay-info-dev hwdata libpulse-dev
	libpango1.0-dev libcairo2-dev libglib2.0-dev libncursesw5-dev
	libfcft-dev
	zsh
	foot swaybg mako-notifier brightnessctl playerctl swaylock
	grim slurp wl-clipboard wlr-randr imagemagick
	xdg-desktop-portal xdg-desktop-portal-gtk xwayland
	adwaita-icon-theme hicolor-icon-theme papirus-icon-theme
	sassc
	wlsunset pulsemixer bluez
	wireguard-tools jq libnotify-bin
	nftables e2fsprogs
	fontconfig curl unzip ca-certificates"
PIPEWIRE_void="pipewire wireplumber"
PIPEWIRE_arch="pipewire pipewire-pulse wireplumber"
PIPEWIRE_debian="pipewire pipewire-pulse wireplumber"

install_deps() {
	eval "pkgs=\$PKGS_$DISTRO"
	have_pipewire || eval "pkgs=\"\$pkgs \$PIPEWIRE_$DISTRO\""
	# Append per-host extras (hosts/<host>/pkgs.<distro>), if present.
	host_pkgs_file="$SRC/hosts/$HOST/pkgs.$DISTRO"
	if [ -f "$host_pkgs_file" ]; then
		host_pkgs=$(grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$host_pkgs_file" | tr '\n' ' ')
		[ -n "$host_pkgs" ] && pkgs="$pkgs $host_pkgs"
	fi
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

# ---- wayfreeze (Wayland screen-freeze for screenshot-area) ----
# Cargo-built (not in distro repos). Upstream 0.2.0 hardcodes exit-on-Esc and
# exit-on-mouse-up handlers, so the first Esc kills wayfreeze before slurp
# sees it — we strip those state.exit assignments at build time.
install_wayfreeze() {
	have wayfreeze && return
	have cargo || { warn "cargo missing — wayfreeze skipped (screenshot will use live capture)"; return; }
	say "building wayfreeze (patched: no exit-on-input)"
	tmp=$(mktemp -d)
	if ! git clone --depth=1 https://github.com/Jappie3/wayfreeze "$tmp/wayfreeze" >/dev/null 2>&1; then
		warn "wayfreeze clone failed"; rm -rf "$tmp"; return
	fi
	sed -i '/Mouse button released - exiting/{n;d;}' "$tmp/wayfreeze/src/main.rs"
	sed -i '/Escape pressed - exiting/{n;d;}'         "$tmp/wayfreeze/src/main.rs"
	(cd "$tmp/wayfreeze" && cargo build --release --quiet >/dev/null 2>&1) \
		&& install -Dm755 "$tmp/wayfreeze/target/release/wayfreeze" "$HOME/.local/bin/wayfreeze" \
		|| warn "wayfreeze build failed"
	rm -rf "$tmp"
}

# ---- themes (Graphite GTK + Bibata cursor) ----
# Graphite is regenerated from main when missing (no version pin — theme files
# stable, sassc compiles SCSS at install). Bibata pins a release tarball and
# extracts only the requested cursor variant.
BIBATA_VER=v2.0.7
ensure_themes() {
	# Graphite-Dark (--tweaks black). Skip if a fully-built variant already exists.
	if [ ! -f "$HOME/.themes/Graphite-Dark/gtk-3.0/gtk.css" ]; then
		# Older partial install? Wipe the empty shell so the installer can recreate it.
		rm -rf "$HOME/.themes/Graphite-Dark"
		if ! have sassc; then
			warn "sassc missing — Graphite cannot compile CSS; install sassc and re-run"
		else
			say "installing Graphite-gtk-theme (Dark/black)"
			tmp=$(mktemp -d)
			if git clone --depth=1 https://github.com/vinceliuice/Graphite-gtk-theme.git \
					"$tmp/graphite" >/dev/null 2>&1; then
				(cd "$tmp/graphite" && \
					./install.sh -d "$HOME/.themes" -t default -c dark \
						-s standard --tweaks black >/dev/null 2>&1) \
					|| warn "graphite install.sh failed"
			else
				warn "graphite clone failed — check network"
			fi
			rm -rf "$tmp"
		fi
	fi

	# Bibata-Modern-Ice — single variant only.
	if [ ! -d "$HOME/.icons/Bibata-Modern-Ice" ]; then
		say "installing Bibata-Modern-Ice cursor ($BIBATA_VER)"
		tmp=$(mktemp -d)
		url="https://github.com/ful1e5/Bibata_Cursor/releases/download/$BIBATA_VER/Bibata-Modern-Ice.tar.xz"
		if fetch "$url" "$tmp/bibata.tar.xz"; then
			mkdir -p "$HOME/.icons"
			tar -xf "$tmp/bibata.tar.xz" -C "$HOME/.icons" \
				|| warn "bibata extract failed"
		else
			warn "bibata download failed — install curl or wget"
		fi
		rm -rf "$tmp"
	fi

	# Defensive: a partial user-level Papirus-Dark in ~/.local/share/icons gets
	# preferred over /usr/share/icons and crashes apps when files are missing.
	if [ -d "$HOME/.local/share/icons/Papirus-Dark" ] \
	   && [ ! -d "$HOME/.local/share/icons/Papirus-Dark/scalable" ]; then
		warn "removing partial $HOME/.local/share/icons/Papirus-Dark (shadows /usr/share)"
		rm -rf "$HOME/.local/share/icons/Papirus-Dark"
	fi
}

# `install -D` but only when dst is missing or its contents differ. Pairs
# with make's mtime tracking so `--rebuild` no-ops when nothing changed.
# $1 may be empty (no-op prefix) or the value of $SUDO. Routing cmp through
# the same prefix covers root-only destinations (e.g. /etc/sudoers.d).
install_if_changed() {
	# usage: install_if_changed [SUDO|""] MODE SRC DST
	sc=$1 mode=$2 src=$3 dst=$4
	$sc cmp -s "$src" "$dst" 2>/dev/null && return 0
	$sc install -Dm"$mode" "$src" "$dst"
}

# Sed-substitute a *.in template and install the result if it differs from
# the existing destination. Extra args are passed through to sed (use -e).
install_template() {
	# usage: install_template [SUDO|""] MODE SRC.in DST [-e SEDEXPR]...
	sc=$1 mode=$2 src=$3 dst=$4
	shift 4
	tmp=$(mktemp)
	sed "$@" "$src" > "$tmp"
	install_if_changed "$sc" "$mode" "$tmp" "$dst"
	rm -f "$tmp"
}

# Enable a runit service by symlinking /etc/sv/NAME to /var/service/NAME.
# No-op on non-Void distros (caller wires systemd if they want).
sv_enable() {
	[ "$DISTRO" = void ] || return 0
	name=$1
	[ -L "/var/service/$name" ] || $SUDO ln -sf "/etc/sv/$name" "/var/service/$name"
}

# Papirus-Dark folder accent. The tool is idempotent but rewrites thousands of
# symlinks (~10s), so cache the last-applied color and short-circuit when it
# hasn't changed. Full-install only — recoloring isn't part of the --rebuild
# loop, and config.h-driven changes still apply on the next plain install.
apply_papirus_color() {
	have papirus-folders || return 0
	[ -d /usr/share/icons/Papirus-Dark ] || return 0
	color=$(read_str WS_PAPIRUS_FOLDER); color=${color:-grey}
	cache="${XDG_CACHE_HOME:-$HOME/.cache}/dwlarp/papirus-folder-color"
	[ "$(cat "$cache" 2>/dev/null)" = "$color" ] && return 0
	say "applying papirus folder color ($color)"
	if $SUDO papirus-folders -C "$color" -t Papirus-Dark >/dev/null 2>&1; then
		mkdir -p "$(dirname "$cache")"
		printf '%s\n' "$color" > "$cache"
	else
		warn "papirus-folders failed (color=$color)"
	fi
}

# ---- C projects ----
# Incremental: make's mtime tracking handles the compile, and we skip the
# `make install` copy entirely when the artifact already matches what's
# installed. Pass FORCE=1 (or `--rebuild --force`) to nuke artifacts and
# recompile from scratch.
build_install() {
	# usage: build_install [SUDO|""] NAME DIR DST PREFIX
	sc=$1 name=$2 dir=$3 dst=$4 prefix=$5
	say "building $name"
	[ "${FORCE:-0}" = 1 ] && make -C "$dir" clean >/dev/null 2>&1 || true
	make -C "$dir"
	if $sc cmp -s "$dir/$name" "$dst" 2>/dev/null; then
		printf '    %s up to date\n' "$name"
		return 0
	fi
	$sc make -C "$dir" PREFIX="$prefix" install
}
build_all() {
	build_install "$SUDO" dwl             "$SRC/dwl"             /usr/bin/dwl                       /usr
	build_install "$SUDO" dwlb            "$SRC/dwlb"            /usr/bin/dwlb                      /usr
	build_install ""      dwlb-status     "$SRC/dwlb-status"     "$HOME/.local/bin/dwlb-status"     "$HOME/.local"
	build_install ""      dwlb-leftstatus "$SRC/dwlb-leftstatus" "$HOME/.local/bin/dwlb-leftstatus" "$HOME/.local"
	build_install ""      ws-hud          "$SRC/ws-hud"          "$HOME/.local/bin/ws-hud"          "$HOME/.local"
	build_install "$SUDO" mullvad-menu    "$SRC/mullvad-menu"    /usr/local/bin/mullvad-menu        /usr/local
}

# ---- scripts (single source of truth, used by full install AND --rebuild) ----
SCRIPTS="dwl-autostart dwl-wallpaper dwl-autolayout dwl-watch-outputs screenshot-area dmenu-launcher dwl-osd ws-pomodoro ws-powermenu ws-hud-lidlock"
install_scripts() {
	mkdir -p "$HOME/.local/bin"
	for s in $SCRIPTS; do
		install_if_changed "" 755 "$SRC/scripts/$s" "$HOME/.local/bin/$s"
	done
	rm -f "$HOME/.local/bin/bemenu-desktop"  # legacy launcher
	# /usr/bin so ly's PATH (which lists /usr/bin before /usr/local/bin) picks
	# up the dbus-wrapping launcher.
	install_if_changed "$SUDO" 755 "$SRC/scripts/dwlarp"         /usr/bin/dwlarp
	install_if_changed "$SUDO" 644 "$SRC/desktop/dwlarp.desktop" /usr/share/wayland-sessions/dwlarp.desktop
	# Drop legacy entries from earlier installs.
	$SUDO rm -f /usr/local/bin/dwl-session /usr/bin/dwl-session \
		/usr/share/wayland-sessions/dwl.desktop

	# Mullvad helpers — root-owned in /usr/local/bin so sudoers whitelists by
	# absolute path. Bootstrap once: sudo mullvad-wg-setup <ACCOUNT_NUMBER>.
	install_if_changed "$SUDO" 755 "$SRC/scripts/mullvad-wg-setup"  /usr/local/bin/mullvad-wg-setup
	install_template   "$SUDO" 755 "$SRC/scripts/mullvad-vpn.in"    /usr/local/bin/mullvad-vpn \
		-e "s|@WS_VPN_KEEPALIVE@|$(read_num WS_VPN_KEEPALIVE)|g" \
		-e "s|@WS_VPN_STALE_S@|$(read_num WS_VPN_STALE_S)|g"     \
		-e "s|@WS_VPN_KILLSWITCH@|$(read_num WS_VPN_KILLSWITCH)|g" \
		-e "s|@WS_VPN_REGION@|$(read_str WS_VPN_REGION)|g"
	install_template   "$SUDO" 755 "$SRC/scripts/mullvad-watchdog.in" /usr/local/bin/mullvad-watchdog \
		-e "s|@WS_VPN_STALE_S@|$(read_num WS_VPN_STALE_S)|g"
	install_if_changed "$SUDO" 440 "$SRC/assets/sudoers-ws-mullvad" /etc/sudoers.d/ws-mullvad
	install_if_changed "$SUDO" 644 "$SRC/assets/mullvad-killswitch.nft" \
		/etc/nftables.d/mullvad-killswitch.nft

	# Persistent relay cache directory. /var/lib/mullvad/relays.tsv replaces
	# the old /run/mullvad.relays (tmpfs, lost on every reboot — that lost
	# cache was the actual cause of the "missing relay cache and killswitch
	# is on" boot loop). Migrate any existing /run copy so the user doesn't
	# have to re-run `mullvad-wg-setup relays` after upgrading.
	$SUDO install -d -m 755 /var/lib/mullvad
	if [ -s /run/mullvad.relays ] && [ ! -s /var/lib/mullvad/relays.tsv ]; then
		$SUDO cp /run/mullvad.relays /var/lib/mullvad/relays.tsv
		$SUDO chmod 644 /var/lib/mullvad/relays.tsv
	fi

	# Runit services for the watchdog + (optional) killswitch loader.
	# /etc/sv/<name>/run + /etc/sv/<name>/log/run, enabled via /var/service.
	# Non-Void distros: scripts/ruleset are installed but the service-enable
	# step is a no-op; wire systemd units yourself if you want auto-start.
	install_if_changed "$SUDO" 755 "$SRC/assets/sv-mullvad-watchdog/run"     /etc/sv/mullvad-watchdog/run
	install_if_changed "$SUDO" 755 "$SRC/assets/sv-mullvad-watchdog/log/run" /etc/sv/mullvad-watchdog/log/run
	install_if_changed "$SUDO" 755 "$SRC/assets/sv-nftables-mullvad/run"     /etc/sv/nftables-mullvad/run
	install_if_changed "$SUDO" 755 "$SRC/assets/sv-nftables-mullvad/finish"  /etc/sv/nftables-mullvad/finish
	install_if_changed "$SUDO" 755 "$SRC/assets/sv-nftables-mullvad/log/run" /etc/sv/nftables-mullvad/log/run
	[ "$(read_num WS_VPN_WATCHDOG)" = 1 ]   && sv_enable mullvad-watchdog
	[ "$(read_num WS_VPN_KILLSWITCH)" = 1 ] && sv_enable nftables-mullvad

	# elogind sleep hooks: re-unblock wifi+bluetooth on resume, and force a
	# Mullvad re-rank+up after the WireGuard handshake almost certainly died
	# during suspend (NAT mapping gone, peer might have churned).
	install_if_changed "$SUDO" 755 "$SRC/assets/elogind-rfkill-unblock" \
		/usr/libexec/elogind/system-sleep/99-rfkill-unblock
	install_if_changed "$SUDO" 755 "$SRC/assets/elogind-resume-mullvad" \
		/usr/libexec/elogind/system-sleep/98-mullvad-resume
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

	# GTK theme/icon/cursor wiring. Same content for gtk-3.0 and gtk-4.0;
	# libadwaita ignores settings.ini for theming, so symlink the chosen
	# theme's gtk-4.0 CSS in (Graphite supports this; not all themes do).
	gtk_theme=$(read_str WS_GTK_THEME)
	icon_theme=$(read_str WS_ICON_THEME)
	cursor_theme=$(read_str WS_CURSOR_THEME)
	cursor_size=$(read_num WS_CURSOR_SIZE)
	gtk_font=$(read_str WS_GTK_FONT)

	mkdir -p "$cfg/gtk-3.0" "$cfg/gtk-4.0" "$cfg/dwlarp"
	for v in 3.0 4.0; do
		sed -e "s|@WS_GTK_THEME@|$gtk_theme|g"       \
		    -e "s|@WS_ICON_THEME@|$icon_theme|g"     \
		    -e "s|@WS_CURSOR_THEME@|$cursor_theme|g" \
		    -e "s|@WS_CURSOR_SIZE@|$cursor_size|g"   \
		    -e "s|@WS_GTK_FONT@|$gtk_font|g"         \
		    "$SRC/assets/gtk.settings.ini.in" > "$cfg/gtk-$v/settings.ini"
	done
	if [ -f "$HOME/.themes/$gtk_theme/gtk-4.0/gtk.css" ]; then
		ln -sf "$HOME/.themes/$gtk_theme/gtk-4.0/gtk.css"      "$cfg/gtk-4.0/gtk.css"
		ln -sf "$HOME/.themes/$gtk_theme/gtk-4.0/gtk-dark.css" "$cfg/gtk-4.0/gtk-dark.css"
	fi

	sed -e "s|@WS_CURSOR_THEME@|$cursor_theme|g" \
	    -e "s|@WS_CURSOR_SIZE@|$cursor_size|g"   \
	    "$SRC/assets/dwlarp.env.in" > "$cfg/dwlarp/env"

	# gsettings — xdg-desktop-portal-gtk reads these to advertise the theme
	# to portal-aware clients (librewolf chrome, electron, …). Failures
	# (no dbus, no schema) are non-fatal — settings.ini still applies.
	if have gsettings; then
		gsettings set org.gnome.desktop.interface gtk-theme    "$gtk_theme"    2>/dev/null || true
		gsettings set org.gnome.desktop.interface icon-theme   "$icon_theme"   2>/dev/null || true
		gsettings set org.gnome.desktop.interface cursor-theme "$cursor_theme" 2>/dev/null || true
		gsettings set org.gnome.desktop.interface cursor-size  "$cursor_size"  2>/dev/null || true
		gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark'   2>/dev/null || true
	fi
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

set_default_shell() {
	have zsh || { warn "zsh missing — skipping default-shell change"; return 0; }
	zsh_path=$(command -v zsh)
	current=$(getent passwd "$USER" 2>/dev/null | cut -d: -f7)
	[ "$current" = "$zsh_path" ] && return 0
	say "setting login shell to $zsh_path for $USER (current: $current)"
	if $SUDO chsh -s "$zsh_path" "$USER" 2>/dev/null; then
		printf '    log out and back in for it to take effect\n'
	else
		warn "chsh failed — run manually: chsh -s $zsh_path"
	fi
}

# Append a tty1 → dwlarp autostart block to ~/.zprofile (idempotent via
# marker comment). Used when WS_INSTALL_LY=0: without a greeter, the user
# needs *something* to bring up the session at login. ~/.zprofile is the
# right hook because set_default_shell just made zsh the login shell.
ensure_tty1_autostart() {
	profile="$HOME/.zprofile"
	marker="# dwlarp: tty1 → dwlarp autostart"
	if [ -f "$profile" ] && grep -qF "$marker" "$profile"; then
		return 0
	fi
	say "adding tty1 dwlarp autostart to $profile"
	cat >> "$profile" <<EOF

$marker
if [ -z "\$WAYLAND_DISPLAY" ] && [ "\$(tty)" = /dev/tty1 ]; then
	exec dwlarp
fi
EOF
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
	ensure_themes
	install_wayfreeze
	seed_configs
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
say "ensuring GTK/icon/cursor themes"; ensure_themes; apply_papirus_color
install_wayfreeze
say "seeding configs and scripts"; seed_configs; install_scripts
say "installing fonts"; install_fonts
if [ "$(read_num WS_INSTALL_LY)" = 0 ]; then
	say "ly: skipped (WS_INSTALL_LY=0). Falling back to tty1 autostart."
	ensure_tty1_autostart
else
	say "installing ly greeter"
	install_ly || { warn "ly install failed — falling back to tty1 autostart"; ensure_tty1_autostart; }
fi

set_default_shell

case ":${PATH}:" in *":$HOME/.local/bin:"*) ;;
	*) warn "$HOME/.local/bin not in PATH — add it to your shell rc" ;;
esac

say "done — pick 'dwlarp' at the greeter (or run dwlarp from a TTY)"
say "rebuild after editing config.h:  ./install.sh --rebuild"
if [ "$DISTRO" = void ]; then
	say "mullvad: bootstrap once with  sudo mullvad-wg-setup <ACCOUNT>"
	say "mullvad: runit services (mullvad-watchdog, nftables-mullvad) start automatically within 5s"
	say "mullvad: check killswitch  sudo mullvad-vpn killswitch status   (default ON; toggle off for captive portals)"
fi
