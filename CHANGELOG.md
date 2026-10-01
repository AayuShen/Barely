# Changelog

All notable changes to Barely are recorded here. Barely is created solely by **AayuShen**.
The format follows [Keep a Changelog](https://keepachangelog.com/), and versions follow
[Semantic Versioning](https://semver.org/).

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
