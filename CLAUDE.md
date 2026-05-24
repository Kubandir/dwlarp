dwlarp: dwl 0.8 (Wayland, suckless) + **twl** (single C daemon: bar + status + HUD + OSD + menu + D-Bus notifications), with a POSIX-sh installer targeting **Void Linux**. Statically configured at compile time.

**twl** replaces the entire old per-component stack (dwlb + dwlb-status + dwlb-leftstatus + ws-hud + mako + dmenu-wl); those sources are archived in `../old/`. twl sources are in `twl/` (see `twl/CLAUDE.md` for daemon internals).

Single source of truth: hosts/$HOST/config.h. The root-level config.h is **generated** by install.sh on every run by copying hosts/$(hostname -s)/config.h to the repo root (gitignored). WS_* macros (compositor colors, fonts, keybinds, app commands, lock, mullvad cadences) live in the per-host file. dwl/config.h and assets/*.in templates read from the generated root config.h (templates are substituted at install time). **twl/config.h is the daemon's own config** — bar/HUD/OSD/menu colors, sizes, HUD button table — separate from hosts/$HOST/config.h.

Multi-host: each host has hosts/<name>/config.h (full config, no inheritance) and optionally hosts/<name>/pkgs.void (extra xbps packages appended to PKGS_void). DWLARP_HOST=name overrides hostname-based auto-pick. WS_FORM_FACTOR ("laptop"|"desktop") is an informational label in each config.h.

Workflow: edit hosts/<host>/config.h (compositor) or twl/config.h (widgets), then ./install.sh -r (--rebuild). The installer copies the host's config.h to root, re-substitutes templates, and rebuilds dwl + twl + mullvad-menu. That's the canonical loop. Don't introduce runtime config files — the suckless model is recompile.

Session boot: `scripts/dwlarp` runs `dwl -s "$HOME/.local/bin/dwl-autostart & $HOME/.local/bin/twl"`. twl owns the bar (top), HUD (hover-revealed buttons), OSD (centered notification slabs), menu (dmenu-replacement, blocks twlctl), and `org.freedesktop.Notifications`. `scripts/dwl-osd` and dbus clients drive it via `twlctl` or D-Bus.

Conventions:
- New user-tunable knobs go in hosts/<host>/config.h as WS_*; downstream components read them via include or installer-time substitution. Never duplicate a knob across components.
- Widget knobs (colors, sizes, HUD buttons, status cadences) go in twl/config.h, not hosts/<host>/config.h.
- Per-host differences belong in hosts/<host>/config.h (different macro values) or hosts/<host>/pkgs.void (extra xbps packages). Don't sprinkle hostname conditionals through install.sh.
- C style follows suckless/dwl upstream: tabs, terse, no narrating comments. Comments only for a non-obvious WHY.
- wlroots 0.19 is required (dwl 0.8). The installer uses the xbps package when available; otherwise builds wlroots from source into /usr/local.

Retired (now in ../old/): dwlb, dwlb-status, dwlb-leftstatus, ws-hud, mako (xbps pkg removed), dmenu-wl (external), dmenu-launcher script, ws-hud-lidlock. WS_BAR_*, WS_HUD_*, WS_STATUS_*, WS_LEFTST_* macros in hosts/*/config.h are no longer consumed (twl reads its own twl/config.h) — left in place for now since apply_theme tolerates orphans.
