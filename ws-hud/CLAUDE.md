# ws-hud

Hover-revealed button HUD that slides down from dwlb. Run `ws-hud` to launch (not autostarted).

All tunables — colours, geometry, timings, animation feel — are in root `config.h` under the **HUD WIDGET (ws-hud)** section as `WS_HUD_*` macros. Edit there + `./install.sh -r`.

The `buttons[]` array (action commands, type CLICK/TOGGLE, nerd-font icon codepoint) lives at the top of `ws-hud.c`. Its length must equal `WS_HUD_BTN_COUNT`.

Deps: `wayland-client`, `fcft`, `pixman-1`. Layer-shell protocol vendored from `../dwlb/protocols/`.
