#!/bin/sh
# dwlarp installer. Supported: Void Linux.
#
# Usage:
#     ./install.sh                full install (greeter, fonts, configs, ly)
#     ./install.sh -r|--rebuild   rebuild C projects only (after editing config.h)
#     ./install.sh -d|--skip-deps skip distro packages
#     ./install.sh -t NAME        switch to themes/NAME.theme and rebuild
#     ./install.sh -t list        list available themes and exit
#     ./install.sh -y             non-interactive
#     ./install.sh -h             help
#
# Curl-pipeable:
#     curl -fsSL https://raw.githubusercontent.com/Kubandir/dwlarp/main/install.sh | sh

set -eu

REBUILD=0; SKIP_DEPS=0; FORCE=${FORCE:-0}; THEME=
while [ $# -gt 0 ]; do
	case "$1" in
		-r|--rebuild)         REBUILD=1 ;;
		-d|--skip-deps)       SKIP_DEPS=1 ;;
		-f|--force)           FORCE=1 ;;
		-y|--noconfirm)       ;;   # accepted for compatibility; install is non-interactive
		-t|--theme)           [ $# -ge 2 ] || { printf '%s: %s needs an argument\n' "$0" "$1" >&2; exit 2; }
		                      THEME=$2; REBUILD=1; shift ;;
		--theme=*)            THEME=${1#*=}; REBUILD=1 ;;
		-h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) printf 'unknown flag: %s\n' "$1" >&2; exit 2 ;;
	esac
	shift
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

# ---- theme overlay ----
# A theme is a POSIX-sh fragment in themes/<name>.theme that pins every
# user-visible color (compositor, bar, lock, status text, HUD, foot)
# plus a wallpaper path. apply_theme sed-patches the WS_*
# color macros in the just-copied root config.h, and exports the rest
# of the vars into this shell so seed_configs / install_scripts can use
# them when rendering templates. The active theme name is cached so a
# bare `./install.sh --rebuild` re-applies the same theme.
THEME_CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/dwlarp/active-theme

if [ "$THEME" = list ]; then
	ls "$SRC/themes" 2>/dev/null | sed -n 's/\.theme$//p'
	exit 0
fi
if [ -z "$THEME" ] && [ -r "$THEME_CACHE" ]; then
	THEME=$(cat "$THEME_CACHE")
fi
THEME=${THEME:-default}
[ -f "$SRC/themes/$THEME.theme" ] || die "no such theme: $THEME (expected $SRC/themes/$THEME.theme)"
say "theme = $THEME"

# Source theme into the current shell. Every var named in the theme
# file is now available to the rest of the installer.
# shellcheck disable=SC1090
. "$SRC/themes/$THEME.theme"

# Patch the just-copied root config.h with the theme's color values.
patch_hex_macro() {  # $1=macro $2=replacement (no 0x/u, just the hex)
	sed -i "s|^\\(#define $1[[:space:]]\\+\\)0x[0-9a-fA-F]\\+|\\10x$2|" "$SRC/config.h"
}
patch_hud_macro() {  # HUD macros end in `u` — preserve it
	sed -i "s|^\\(#define $1[[:space:]]\\+\\)0x[0-9a-fA-F]\\+u|\\10x${2}u|" "$SRC/config.h"
}
patch_str_macro() {  # $1=macro $2=value (verbatim inside the quotes)
	v=$(printf '%s' "$2" | sed 's|[\\&|]|\\&|g')
	sed -i "s|^\\(#define $1[[:space:]]\\+\\)\"[^\"]*\"|\\1\"$v\"|" "$SRC/config.h"
}
for m in WS_BG WS_BORDER WS_FOCUS WS_URGENT \
         WS_BAR_BG WS_BAR_FG WS_BAR_ACTIVE_BG WS_BAR_URGENT_BG; do
	eval v=\${$m:-}; [ -n "$v" ] && patch_hex_macro "$m" "$v"
done
for m in WS_HUD_BG WS_HUD_FG WS_HUD_ON WS_HUD_HOLD WS_HUD_WARN WS_HUD_ICON; do
	eval v=\${$m:-}; [ -n "$v" ] && patch_hud_macro "$m" "$v"
done
for m in WS_LOCK_SCREEN_HEX WS_LOCK_RING_HEX WS_LOCK_TEXT_HEX WS_LOCK_WRONG_HEX \
         WS_STATUS_FG WS_STATUS_SEP WS_LEFTST_FG WS_LEFTST_DATE_FG \
         WS_STATUS_VPN_ON_FG WS_STATUS_VPN_STALE_FG WS_STATUS_VPN_OFF_FG; do
	eval v=\${$m:-}; [ -n "$v" ] && patch_str_macro "$m" "$v"
done
[ -n "${WALLPAPER:-}" ] && patch_str_macro WS_WALLPAPER "$WALLPAPER"

mkdir -p "$(dirname "$THEME_CACHE")"
printf '%s\n' "$THEME" > "$THEME_CACHE"

# ---- distro detection ----
. /etc/os-release
case "${ID:-}${ID_LIKE:-}" in
	*void*) DISTRO=void ;;
	*) die "unsupported distro: ${PRETTY_NAME:-unknown} (Void Linux only)" ;;
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
	fcft-devel
	zsh zsh-autosuggestions zsh-completions zsh-history-substring-search zsh-syntax-highlighting
	elogind seatd
	foot
	grim slurp wl-clipboard wlr-randr
	stb
	xdg-desktop-portal xdg-desktop-portal-gtk xdg-desktop-portal-wlr
	papirus-icon-theme papirus-folders
	sassc
	xorg-server-xwayland fontconfig
	pam-devel
	bluetuith impala pulsemixer
	wireguard-tools jq libnotify
	nftables e2fsprogs
	curl unzip"
PIPEWIRE_void="pipewire wireplumber"

install_deps() {
	pkgs=$PKGS_void
	have_pipewire || pkgs="$pkgs $PIPEWIRE_void"
	# Append per-host extras (hosts/<host>/pkgs.void), if present.
	host_pkgs_file="$SRC/hosts/$HOST/pkgs.void"
	if [ -f "$host_pkgs_file" ]; then
		host_pkgs=$(grep -vE '^[[:space:]]*#|^[[:space:]]*$' "$host_pkgs_file" | tr '\n' ' ')
		[ -n "$host_pkgs" ] && pkgs="$pkgs $host_pkgs"
	fi
	# librewolf isn't in Void's official repos. If it's in the package list,
	# drop the index-0/librewolf-void community repo before the sync.
	case " $pkgs " in
		*' librewolf '*)
			if [ ! -f /etc/xbps.d/20-librewolf.conf ]; then
				say "adding librewolf-void xbps repo (index-0/librewolf-void)"
				printf 'repository=https://github.com/index-0/librewolf-void/releases/latest/download/\n' \
					| $SUDO tee /etc/xbps.d/20-librewolf.conf >/dev/null
			fi ;;
	esac
	$SUDO xbps-install -Sy >/dev/null || warn "repo sync failed"
	missing=
	for p in $pkgs; do
		xbps-query -p pkgver "$p" >/dev/null 2>&1 && continue
		if xbps-query -R "$p" >/dev/null 2>&1; then
			missing="$missing $p"
		else
			warn "package '$p' not in repos — skipping"
		fi
	done
	[ -n "$missing" ] && { $SUDO xbps-install -y $missing || die "xbps-install failed"; }
	$SUDO xbps-install -y wlroots0.19 wlroots0.19-devel 2>/dev/null \
		|| $SUDO xbps-install -y wlroots-devel 2>/dev/null \
		|| warn "wlroots not in repos — will build from source"
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
	# twl ships two binaries; build_install only checks the first. Install the
	# whole target tree so both twl and twlctl land in ~/.local/bin.
	say "building twl"
	[ "${FORCE:-0}" = 1 ] && make -C "$SRC/twl" distclean >/dev/null 2>&1 || true
	make -C "$SRC/twl"
	make -C "$SRC/twl" PREFIX="$HOME/.local" install
	build_install "$SUDO" mullvad-menu    "$SRC/mullvad-menu"    /usr/local/bin/mullvad-menu        /usr/local
}

# ---- scripts (single source of truth, used by full install AND --rebuild) ----
SCRIPTS="dwl-autostart dwl-autolayout dwl-launcher screenshot-area"
# Plain copies (no template substitution — twlctl menu is the only menu now,
# and twl owns its own colors via twl/config.h).
PLAIN_IN_SCRIPTS="ws-pomodoro"
install_scripts() {
	mkdir -p "$HOME/.local/bin"
	for s in $SCRIPTS; do
		install_if_changed "" 755 "$SRC/scripts/$s" "$HOME/.local/bin/$s"
	done
	for s in $PLAIN_IN_SCRIPTS; do
		install_if_changed "" 755 "$SRC/scripts/$s.in" "$HOME/.local/bin/$s"
	done
	rm -f "$HOME/.local/bin/bemenu-desktop"  # legacy launcher
	rm -f "$HOME/.local/bin/dmenu-launcher" "$HOME/.local/bin/ws-hud-lidlock"  # replaced by twl
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
	# foot.ini is rendered from the active theme's vars. twl colors live in
	# twl/config.h (compiled in), so there is no per-theme mako/dmenu config
	# left to render. install_template only rewrites the dst when the
	# rendered output differs, so this is a no-op when the user re-runs the
	# same theme.
	mkdir -p "$cfg/foot"
	install_template "" 644 "$SRC/assets/foot.ini.in" "$cfg/foot/foot.ini" \
		-e "s|@T_ALPHA@|$T_ALPHA|g" \
		-e "s|@T_BG@|$T_BG|g"       -e "s|@T_FG@|$T_FG|g" \
		-e "s|@T_SELFG@|$T_SELFG|g" -e "s|@T_SELBG@|$T_SELBG|g" \
		-e "s|@T_R0@|$T_R0|g" -e "s|@T_R1@|$T_R1|g" -e "s|@T_R2@|$T_R2|g" -e "s|@T_R3@|$T_R3|g" \
		-e "s|@T_R4@|$T_R4|g" -e "s|@T_R5@|$T_R5|g" -e "s|@T_R6@|$T_R6|g" -e "s|@T_R7@|$T_R7|g" \
		-e "s|@T_B0@|$T_B0|g" -e "s|@T_B1@|$T_B1|g" -e "s|@T_B2@|$T_B2|g" -e "s|@T_B3@|$T_B3|g" \
		-e "s|@T_B4@|$T_B4|g" -e "s|@T_B5@|$T_B5|g" -e "s|@T_B6@|$T_B6|g" -e "s|@T_B7@|$T_B7|g"
	rm -rf "$cfg/mako"

	# xdg-desktop-portal: tell it to route FileChooser/AppChooser/Settings to
	# the gtk backend (file pickers) and screencast/screenshot to wlr.
	$SUDO install -Dm644 "$SRC/assets/dwl-portals.conf" \
		/usr/share/xdg-desktop-portal/dwl-portals.conf

	# swaylock retired — twl owns session lock via ext_session_lock_v1.
	rm -rf "$cfg/swaylock"

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

	# GPU-specific env. WS_GPU=nvidia → wlroots needs the vulkan renderer
	# (gles2 has black-screen + cursor-disappear bugs on the proprietary
	# driver) and SW cursors; VA-API/GBM/GLX are pointed at the proprietary
	# stack so browsers/players pick the right ICD.
	case "$(read_str WS_GPU)" in
		nvidia) cat >> "$cfg/dwlarp/env" <<'EOF'

export WLR_RENDERER=vulkan
export WLR_NO_HARDWARE_CURSORS=1
export LIBVA_DRIVER_NAME=nvidia
export GBM_BACKEND=nvidia-drm
export __GLX_VENDOR_LIBRARY_NAME=nvidia
EOF
			;;
	esac

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
	[ -L /var/service/ly ] || $SUDO ln -s /etc/sv/ly /var/service/ly
	say "ly enabled — reboot to take effect"
}

ly_build_from_source() {
	$SUDO xbps-install -Sy zig pam-devel libxcb-devel >/dev/null 2>&1 || true
	have zig || { warn "ly: zig unavailable — skipping"; return 1; }
	tmp=$(mktemp -d)
	git clone --depth=1 https://github.com/fairyglade/ly.git "$tmp/ly" \
		|| { warn "ly: clone failed"; return 1; }
	(
		cd "$tmp/ly"
		zig build
		$SUDO zig build installexe -Dinit_system=runit
	) || { warn "ly: build failed (zig version mismatch?)"; return 1; }
}

# Void-only: enable elogind + dbus runit services and wire pam_elogind
# into /etc/pam.d/system-login.  Without this, agetty logins don't get
# XDG_RUNTIME_DIR and dwl exits immediately with "XDG_RUNTIME_DIR must
# be set".  arch/debian have systemd-logind doing this natively, so the
# function is a no-op on those.
ensure_session_manager() {
	[ "$DISTRO" = void ] || return 0
	have elogind || have loginctl || { warn "elogind missing — XDG_RUNTIME_DIR won't be set at login"; return 0; }
	sv_enable elogind
	sv_enable dbus
	# seatd as the libseat fallback. libseat tries logind→seatd→builtin;
	# without seatd running, dwl bails with "could not connect to socket
	# /run/seatd.sock" when the elogind session is briefly not "active"
	# at compositor start (race during TTY autostart).
	sv_enable seatd
	# Membership in _seatd is required to open /run/seatd.sock (mode 0660).
	if getent group _seatd >/dev/null 2>&1 \
	   && ! id -nG "$USER" 2>/dev/null | tr ' ' '\n' | grep -qx _seatd; then
		say "adding $USER to _seatd group (re-login required)"
		$SUDO usermod -aG _seatd "$USER"
	fi
	if [ -f /etc/pam.d/system-login ] && ! grep -q pam_elogind /etc/pam.d/system-login; then
		say "wiring pam_elogind into /etc/pam.d/system-login"
		echo '-session   optional   pam_elogind.so' | $SUDO tee -a /etc/pam.d/system-login >/dev/null
	fi
}

set_default_shell() {
	have zsh || { warn "zsh missing — skipping default-shell change"; return 0; }
	# pam_shells does a literal string match against /etc/shells (no symlink
	# resolution), so the path we hand to chsh MUST appear verbatim there or
	# login/ly/swaylock will reject the user. command -v can return /sbin/zsh
	# on Void (where /sbin → usr/bin), which is typically absent from /etc/shells.
	zsh_path=
	for cand in /bin/zsh /usr/bin/zsh "$(command -v zsh)"; do
		[ -x "$cand" ] || continue
		if grep -qxF "$cand" /etc/shells 2>/dev/null; then
			zsh_path=$cand
			break
		fi
		[ -z "$zsh_path" ] && zsh_path=$cand
	done
	[ -n "$zsh_path" ] || { warn "no usable zsh path found"; return 0; }
	if ! grep -qxF "$zsh_path" /etc/shells 2>/dev/null; then
		say "adding $zsh_path to /etc/shells"
		echo "$zsh_path" | $SUDO tee -a /etc/shells >/dev/null
	fi
	current=$(getent passwd "$USER" 2>/dev/null | cut -d: -f7)
	[ "$current" = "$zsh_path" ] && return 0
	say "setting login shell to $zsh_path for $USER (current: $current)"
	if $SUDO chsh -s "$zsh_path" "$USER" 2>/dev/null; then
		printf '    log out and back in for it to take effect\n'
	else
		warn "chsh failed — run manually: chsh -s $zsh_path"
	fi
}

# Append a tty1 → dwlarp autostart block to the login profile (idempotent via
# marker comment). Used when WS_INSTALL_LY=0: without a greeter, the user
# needs *something* to bring up the session at login.
#
# Path selection: set_default_shell just made zsh the login shell, and on Void
# /etc/zsh/zshenv exports ZDOTDIR=$HOME/.config/zsh — so zsh reads
# $ZDOTDIR/.zprofile, NOT $HOME/.zprofile. We source the system zshenv to
# resolve the actual ZDOTDIR for this user; falling back to $HOME for non-zsh
# or unset cases.
ensure_tty1_autostart() {
	profile="$HOME/.zprofile"
	if command -v zsh >/dev/null 2>&1; then
		zdotdir=$(
			ZDOTDIR=$HOME
			[ -r /etc/zsh/zshenv ] && . /etc/zsh/zshenv 2>/dev/null
			printf %s "$ZDOTDIR"
		)
		[ -n "$zdotdir" ] && [ -d "$zdotdir" ] && profile="$zdotdir/.zprofile"
	fi
	marker="# dwlarp: tty1 → dwlarp autostart"
	if [ -f "$profile" ] && grep -qF "$marker" "$profile"; then
		return 0
	fi
	say "adding tty1 dwlarp autostart to $profile"
	cat >> "$profile" <<EOF

$marker
if [ -z "\$WAYLAND_DISPLAY" ] && [ "\$(tty)" = /dev/tty1 ] && command -v dwlarp >/dev/null 2>&1; then
	# Run dwlarp as a child (not exec'd into the login shell) so that
	# when it exits the shell can unwind normally and PAM closes the
	# session. With \`exec\` the login chain has no parent left to call
	# pam_close_session, elogind never garbage-collects the session,
	# and the next login spits "failed to add session N to hash map:
	# file exists" before falling through to a new ID.
	# The \`command -v dwlarp\` guard keeps this block safe to live in a
	# dotfile-synced \$ZDOTDIR/.zprofile shared across hosts that don't
	# all have dwlarp installed.
	dwlarp
	exit
fi
EOF
}

install_ly() {
	have ly && { say "ly already installed"; ly_enable; return; }
	$SUDO xbps-install -Sy ly 2>/dev/null && have ly && { ly_enable; return; }
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

say "dwlarp installer — Void Linux"
[ "$SKIP_DEPS" -eq 0 ] && { say "installing distro packages"; install_deps; }
say "ensuring wlroots 0.19"; ensure_wlroots
export PKG_CONFIG_PATH="$WLR_PREFIX/lib/pkgconfig:$WLR_PREFIX/lib64/pkgconfig:$WLR_PREFIX/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
build_all
say "ensuring GTK/icon/cursor themes"; ensure_themes; apply_papirus_color
install_wayfreeze
say "seeding configs and scripts"; seed_configs; install_scripts
say "installing fonts"; install_fonts
say "wiring elogind/dbus for XDG_RUNTIME_DIR at login"; ensure_session_manager

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
say "mullvad: bootstrap once with  sudo mullvad-wg-setup <ACCOUNT>"
say "mullvad: runit services (mullvad-watchdog, nftables-mullvad) start automatically within 5s"
say "mullvad: check killswitch  sudo mullvad-vpn killswitch status   (default ON; toggle off for captive portals)"
