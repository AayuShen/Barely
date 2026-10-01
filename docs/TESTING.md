# Manual test checklist

Automated tests (`tests/run-tests.ps1`) cover the command line, the security checks and
the build hardening. The items below need a real Windows 11 desktop, because they involve
Explorer. Run Barely from a normal terminal, not from inside a sandboxed or virtualized app.

Check each item with `barely --status` and by looking at the taskbar.

## Basics
- [ ] `barely` makes the taskbar fully clear; `--status` shows "styling N taskbar element(s)".
- [ ] `barely 40`: half-transparent, applied immediately without re-loading.
- [ ] `barely --restore`: stock taskbar comes back immediately.
- [ ] Open Start, Search, Widgets, the system tray overflow and Quick Settings: the taskbar stays as set.

## Options
- [ ] `--tint #FF0000` with `barely 40`: a red tint is visible; `--tint none` removes it.
- [ ] `--border 0`: the thin top line disappears; `--border same` brings it back.
- [ ] `--maximized 100`: maximize a window, and the taskbar turns opaque. Restore or minimize it,
      and the taskbar turns transparent again. Switch between a maximized and a normal window.
- [ ] `--light 60`: switch Windows between light and dark mode (Settings > Personalization >
      Colors); the opacity follows.
- [ ] `--settings`: sliders and checkboxes update the taskbar live; the colour picker works.

## Robustness
- [ ] Theme change, accent colour change, and transparency effects on/off: the setting survives.
- [ ] Sleep and resume, and lock and unlock.
- [ ] Change display scaling (DPI), and connect or disconnect a second monitor.
- [ ] Restart Explorer with `--autostart on`: the setting comes back within a minute.
- [ ] Sign out and in with `--autostart on`.
- [ ] `--diagnose` writes `%LOCALAPPDATA%\Barely\diagnose.txt`.

## Tested Windows builds
Update the table in README.md after a full pass.
