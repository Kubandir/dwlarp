dwlarp: dwl 0.8 (Wayland, suckless) + dwlb (bar) + small status feeders + ws-hud, with one POSIX-sh installer for Void/Arch/Debian-Ubuntu. Statically configured at compile time.

Single source of truth: root config.h. All WS_* macros (colors, fonts, keybinds, app commands, status modules, keyboard, trackpad, bar, HUD geometry) live there. dwl/config.h, dwlb/config.h, the status feeders, and assets/*.in templates all read from it (templates are substituted at install time).

Workflow: edit config.h, then ./install.sh -r (--rebuild). That's the canonical loop. Don't introduce runtime config files — the suckless model is recompile.

Conventions:
- New user-tunable knobs go in root config.h as WS_*; downstream components read them via include or installer-time substitution. Never duplicate a knob across components.
- C style follows suckless/dwl upstream: tabs, terse, no narrating comments. Comments only for a non-obvious WHY.
- wlroots 0.19 is required (dwl 0.8). The installer uses the distro package when available; otherwise builds wlroots from source into /usr/local.

Primary target: Void Linux (maintainer's daily driver). Keep PKGS_void accurate first; arch/debian lists track behind.
