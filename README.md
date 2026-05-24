# dwlarp

dwl 0.8 (Wayland, suckless) + **twl** — a single C daemon that replaces the
entire old widget stack (bar + status + HUD + OSD + menu + D-Bus notifications).
One installer for **Void Linux**. Statically configured at compile time.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/Kubandir/dwlarp/main/install.sh | sh
```

Or clone and run:

```sh
git clone https://github.com/Kubandir/dwlarp
cd dwlarp
./install.sh
```

After it finishes, reboot. agetty autologs into TTY1 and the shell starts
dwlarp automatically.

## Flags

| Flag                 | Effect                                                   |
| -------------------- | -------------------------------------------------------- |
| `--rebuild` / `-r`   | Rebuild C projects + reinstall scripts/configs. No more. |
| `--skip-deps` / `-d` | Skip xbps package installs.                              |
| `-t NAME`            | Switch theme and rebuild (`-t list` to see options).     |
| `-h`                 | Help.                                                    |

## What it installs

| Path                                         | What                                 |
| -------------------------------------------- | ------------------------------------ |
| `/usr/bin/dwl`                               | compositor                           |
| `~/.local/bin/twl`                           | widget daemon (bar/HUD/OSD/menu/notif)|
| `~/.local/bin/twlctl`                        | twl control socket client            |
| `/usr/bin/dwlarp`                            | session entrypoint (`Exec=` in .desktop) |
| `/usr/share/wayland-sessions/dwlarp.desktop` | wayland-session entry (optional)     |
| `~/.local/bin/dwl-autostart`                 | per-session autostart                |
| `~/.local/bin/screenshot-area`               | grim+slurp helper                    |
| `~/.local/share/dwl/wallpaper.png`           | shipped wallpaper                    |

## Configuration

Two config files, never runtime:

- **`hosts/<host>/config.h`** — compositor knobs: colors, fonts, keybinds, app
  commands, lock, mullvad cadences (`WS_*` macros).
- **`twl/config.h`** — widget knobs: bar/HUD/OSD/menu colors, sizes, HUD button
  table, status cadences.

Edit either, then:

```sh
./install.sh --rebuild
```

The installer copies `hosts/$(hostname -s)/config.h` to the repo root (gitignored)
and rebuilds dwl + twl + mullvad-menu. Override the host with `DWLARP_HOST=name`.

## wlroots

dwl 0.8 requires **wlroots 0.19**. The installer tries `xbps-install wlroots0.19`
first; if unavailable it builds from source into `/usr/local`.

## Layout

```
install.sh
hosts/
  <host>/config.h        # per-host compositor config (single source of truth)
  <host>/pkgs.void       # optional extra xbps packages
dwl/                     # dwl 0.8 + btrtile + dwl-ipc-unstable-v2
twl/                     # widget daemon (bar, HUD, OSD, menu, notifications)
mullvad-menu/            # wireguard relay picker
scripts/                 # dwlarp, dwl-autostart, dwl-osd, ws-pomodoro, …
desktop/                 # dwlarp.desktop
assets/                  # wallpaper, foot/gtk templates, runit sv files
themes/                  # *.theme color overlays
```
