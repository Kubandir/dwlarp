ws-hud: hover-revealed button HUD that slides down from dwlb. Run `ws-hud` to launch (autostarted by dwl-autostart).

All visual/timing tunables are WS_HUD_* macros in root config.h. Edit there, then ./install.sh -r.

The buttons[] array (action command, type CLICK or TOGGLE, alt-action for TOGGLEs, nerd-font icon codepoint) is at the top of ws-hud.c. Its length must equal WS_HUD_BTN_COUNT.

Deps: wayland-client, fcft, pixman-1. Layer-shell + xdg-shell protocol XML are vendored from ../dwlb/protocols/.
