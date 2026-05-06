# wayland-suckless

dwl 0.8 (with btrtile BSP) + dwlb + minimal status feeders. One installer for
Void, Arch, and Debian/Ubuntu — including default Debian + GNOME, where it
detects the existing PipeWire stack and leaves it alone.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/Kubandir/wayland-suckless/main/install.sh | sh
```

Or clone and run:

```sh
git clone https://github.com/Kubandir/wayland-suckless
cd wayland-suckless
./install.sh
```

After it finishes, log out, pick **dwl** at the display manager, log in.

## Flags

| Flag                 | Effect                                               |
| -------------------- | ---------------------------------------------------- |
| `--rebuild` / `-r`   | Rebuild C projects + reinstall scripts. Nothing else.|
| `--skip-deps` / `-d` | Don't touch distro packages.                         |
| `-y`                 | Non-interactive.                                     |
| `-h`                 | Help.                                                |

Optional env vars:

- `WITH_LY=1` — install + enable the [ly](https://github.com/fairyglade/ly) greeter (Arch/Void only).

## What it installs

| Path                                          | What                                |
| --------------------------------------------- | ----------------------------------- |
| `/usr/bin/dwl`                                | compositor                          |
| `/usr/bin/dwlb`                               | bar                                 |
| `/usr/local/bin/dwl-session`                  | session entrypoint (used by `Exec=`)|
| `/usr/share/wayland-sessions/dwl.desktop`     | greeter entry                       |
| `~/.local/bin/dwl-autostart`                  | per-session autostart               |
| `~/.local/bin/dwlb-status` / `dwlb-leftstatus`| status feeders                      |
| `~/.local/bin/screenshot-area`                | grim+slurp helper                   |
| `~/.local/share/dwl/wallpaper.png`            | shipped wallpaper                   |

A web browser is **not** installed — the `Win+S` keybind launches whatever
`WS_BROWSER_CMD` is set to in `config.h` (default: `helium`). Edit it to
`firefox` or `chromium` if you prefer, then `./install.sh --rebuild`.

## wlroots

dwl 0.8 needs **wlroots 0.19**. The installer:

1. Tries the distro package (`wlroots0.19` on Arch/Void, `libwlroots-0.19-dev`
   on Debian sid).
2. If unavailable (e.g. Debian trixie ships 0.18), builds wlroots 0.19 from
   `gitlab.freedesktop.org/wlroots` into `/usr/local`.

## PipeWire

If PipeWire is already running (default on modern Debian/Ubuntu/Fedora GNOME),
the installer will not install or restart audio packages. `dwl-autostart` only
launches PipeWire if nothing is running and the binaries exist.

## Configuration

Edit `config.h` at the repo root and re-run `./install.sh --rebuild`. dwl is
statically configured at compile time — that's the suckless way.

## Layout

```
install.sh
config.h              # single user-facing config; everything reads from here
dwl/                  # dwl 0.8 + btrtile + dwl-ipc-unstable-v2
dwlb/                 # bar with IPC + leftstatus extension
dwlb-status/          # libpulse-driven status feeder (1s tick, idle ~0% CPU)
dwlb-leftstatus/      # logo + clock, minute-aligned via timerfd
scripts/              # dwl-session, dwl-autostart, dwl-osd, dmenu-launcher, …
desktop/              # dwl.desktop
assets/               # wallpaper, foot/mako/swaylock/ly templates
```
