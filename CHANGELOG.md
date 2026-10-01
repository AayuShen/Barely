# Changelog

All notable changes to Barely are recorded here. Barely is created solely by **AayuShen**.
The format follows [Keep a Changelog](https://keepachangelog.com/), and versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.1.0] - 2026-10-01

Barely now tells you the truth about what happened, survives Explorer restarts, and has
more ways to style the taskbar.

### Added
- **Tint colour:** `--tint #RRGGBB|none` paints the taskbar background in any colour.
- **Separate border opacity:** `--border <0-100|same>` for the thin top line.
- **Maximized mode:** `--maximized <0-100|off>` switches opacity while the active window is
  maximized. It's event-driven, with hooks installed only when the option is on.
- **Light mode opacity:** `--light <0-100|same>` follows Windows' light/dark setting live.
- **Settings window:** `barely --settings` gives sliders, a colour picker and an autostart
  toggle, all applied live. It's DPI-aware.
- **Auto-repair:** `--autostart on` also adds a scheduled task that re-applies Barely
  within a minute after Explorer restarts. It exits immediately when nothing needs doing.
- **`--status`** shows the Windows build, whether Barely is loaded, how many taskbar
  elements it's styling, the current settings and the autostart state.
- **`--diagnose`** writes a report (`%LOCALAPPDATA%\Barely\diagnose.txt`) listing what
  Barely sees in the taskbar's visual tree, for bug reports.
- Windows build check: a clear error on Windows 10, and a note on untested Windows 11 builds.
- Documented exit codes.
- Automated test suite (`tests/run-tests.ps1`, 36 checks), plus GitHub Actions for builds,
  tests, CodeQL scanning and releases with SHA-256 checksums.
- `build.cmd analyze` (MSVC static analyzer; the code is warning-free), and an optional trace
  log for development.
- Issue templates, a pull request template, CONTRIBUTING.md and a manual test checklist.

### Fixed
- **No more false "success".** Barely now confirms that the DLL found the taskbar
  background, and that the settings it wrote actually reached Explorer. Before, it reported
  success as soon as the DLL was loaded.
- Updating from an older version while it's still loaded in Explorer now explains that you
  need to sign out or restart Explorer, instead of failing confusingly.
- Clear, actionable error messages, with a link to the issue tracker.

### Security
- The DLL is verified before any setting is written.
- `schtasks.exe` is started by its full System32 path; the auto-repair task runs with least
  privilege.
- Window-event hooks (maximized mode) are limited to the foreground process.
- Application manifest: `asInvoker`, per-monitor DPI awareness.

## [1.0.0] - 2026-10-01

First release.

### Added
- Transparent Windows 11 taskbar with no background process: `barely.exe` loads
  `barely_tap.dll` into Explorer once and exits.
- Adjustable opacity, `barely <0-100>`, applied live through a registry watcher with no
  re-injection.
- `--restore` to return to the stock taskbar, `--autostart on|off` for logon,
  `--version`.
- `barelyw.exe`, a windowless build used for autostart so no console window flashes.
- Version resources with author and copyright information in all binaries.
- MIT License.

### Security
- SHA-256 integrity check of `barely_tap.dll`, embedded at build time.
- DLL locked against writes and deletes between verification and load (TOCTOU
  protection).
- Target process verified as `%SystemRoot%\explorer.exe` in the caller's session.
- The DLL only activates inside Explorer.
- Refuses to run elevated.
- DLL search restricted to System32; quoted autostart path.
- Hardened build: `/sdl`, Control Flow Guard, ASLR, DEP, CET, static CRT.
- Single-instance mutex.
