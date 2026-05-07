# wayland-suckless

A dwl 0.8 (Wayland compositor, suckless) + dwlb (bar) setup with a single installer for Void / Arch / Debian-Ubuntu. Statically configured at compile time — suckless way.

## Layout

- `config.h` — **single user-facing config**. All `WS_*` macros (colors, fonts, keybinds, app commands, status modules, keyboard, trackpad, bar). Every downstream component includes this.
- `dwl/` — dwl 0.8 vendored, patched with btrtile (BSP layout) + dwl-ipc-unstable-v2. `dwl/config.h` reads `WS_*` from root `config.h`.
- `dwlb/` — bar with IPC + leftstatus extension. `dwlb/config.h` likewise reads root macros.
- `dwlb-status/` — right-side status feeder (libpulse, 1s tick, idle ~0% CPU): disk/cpu/bat/vol/bright/wifi.
- `dwlb-leftstatus/` — left-side feeder: logo + clock/date, minute-aligned via timerfd.
- `scripts/` — `dwl-session` (entry), `dwl-autostart`, `dwl-osd`, `dmenu-launcher`, `dwl-wallpaper`, `dwl-watch-outputs`, `dwl-autolayout`, `screenshot-area`, `ws-pomodoro`. Installed to `~/.local/bin/` (some to `/usr/local/bin/`).
- `desktop/dwl.desktop` — wayland-sessions entry.
- `assets/` — wallpaper + foot/mako/swaylock/ly config templates (substituted with `WS_*` values at install time).
- `install.sh` — distro detect → packages → wlroots 0.19 (distro pkg or built from source) → build C projects → install binaries/scripts/configs.

## Workflow

User edits `config.h` → runs `./install.sh --rebuild` (or `-r`). That's the canonical loop. Don't add runtime config files; the suckless model is recompile.

## Flags / env

`-r` rebuild only · `-d` skip distro pkgs · `-y` non-interactive · `WITH_LY=1` install ly greeter (Arch/Void).

## Conventions

- New user-tunable knobs go in root `config.h` as `WS_*` macros, then are consumed by `dwl/config.h`, `dwlb/config.h`, status feeders, or `assets/*.in` templates via install-time substitution.
- Don't duplicate config across components — the root `config.h` is the single source of truth.
- C code style follows suckless / dwl upstream (tabs, terse).
- wlroots 0.19 is required (dwl 0.8). Installer falls back to building from source on Debian trixie.
