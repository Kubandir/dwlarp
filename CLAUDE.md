dwlarp: dwl 0.8 (Wayland, suckless) + dwlb (bar) + small status feeders + ws-hud, with one POSIX-sh installer for Void/Arch/Debian-Ubuntu. Statically configured at compile time.

Single source of truth: hosts/$HOST/config.h. The root-level config.h is **generated** by install.sh on every run by copying hosts/$(hostname -s)/config.h to the repo root (gitignored). All WS_* macros (colors, fonts, keybinds, app commands, status modules, keyboard, trackpad, bar, HUD geometry) live in the per-host file. dwl/config.h, dwlb/config.h, status feeders, and assets/*.in templates read from the generated root config.h (templates are substituted at install time).

Multi-host: each host has hosts/<name>/config.h (full config, no inheritance) and optionally hosts/<name>/pkgs.<distro> (extra distro packages appended to PKGS_<distro>). DWLARP_HOST=name overrides hostname-based auto-pick. WS_FORM_FACTOR ("laptop"|"desktop") is an informational label in each config.h.

Workflow: edit hosts/<host>/config.h, then ./install.sh -r (--rebuild). The installer copies the host's config.h to root, re-substitutes templates, and rebuilds. That's the canonical loop. Don't introduce runtime config files — the suckless model is recompile.

Conventions:
- New user-tunable knobs go in hosts/<host>/config.h as WS_*; downstream components read them via include or installer-time substitution. Never duplicate a knob across components.
- Per-host differences belong in hosts/<host>/config.h (different macro values) or hosts/<host>/pkgs.<distro> (extra packages). Don't sprinkle hostname conditionals through install.sh.
- C style follows suckless/dwl upstream: tabs, terse, no narrating comments. Comments only for a non-obvious WHY.
- wlroots 0.19 is required (dwl 0.8). The installer uses the distro package when available; otherwise builds wlroots from source into /usr/local.

Primary target: Void Linux (maintainer's daily driver). Keep PKGS_void accurate first; arch/debian lists track behind.
