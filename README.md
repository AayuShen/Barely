# Barely

**A transparent Windows 11 taskbar with no background process.**

> Barely was created solely by **AayuShen**.
> Copyright (c) 2026 AayuShen. Released under the [MIT License](LICENSE).

[![Build](https://github.com/AayuShen/Barely/actions/workflows/build.yml/badge.svg)](https://github.com/AayuShen/Barely/actions/workflows/build.yml)
[![CodeQL](https://github.com/AayuShen/Barely/actions/workflows/codeql.yml/badge.svg)](https://github.com/AayuShen/Barely/actions/workflows/codeql.yml)
[![Release](https://img.shields.io/github/v/release/AayuShen/Barely)](https://github.com/AayuShen/Barely/releases/latest)

`barely.exe` runs for about a second and exits. It loads a small DLL into
`explorer.exe`, which styles the taskbar from the inside. No tray icon, no service, no
extra process in Task Manager.

<!-- Screenshot: add docs/screenshot.png and uncomment
![Barely on Windows 11](docs/screenshot.png)
-->

## Install

1. Download the latest `Barely-vX.Y.Z-win-x64.zip` from
   [Releases](https://github.com/AayuShen/Barely/releases/latest).
2. Optional: check it against `SHA256SUMS.txt` from the same release:
   `Get-FileHash .\Barely-*.zip -Algorithm SHA256`.
3. Extract it to a folder you'll keep, for example `C:\Tools\Barely`.
4. Open a terminal there and run `.\barely.exe`, or `.\barely.exe --settings` for a window
   with sliders.

> Run Barely from a normal terminal, the Run dialog (Win+R) or File Explorer. Some
> sandboxed or virtualized apps give their child processes a private copy of the
> registry. Barely detects this and tells you, because its settings wouldn't reach
> Explorer.

## Usage

```
barely                       apply saved settings (fully clear by default)
barely <0-100>               taskbar opacity in percent
barely [options]             change one or more settings and apply them:
    --opacity <0-100>          background opacity
    --border <0-100|same>      top border line opacity
    --tint <#RRGGBB|none>      background colour
    --maximized <0-100|off>    opacity while the active window is maximized
    --light <0-100|same>       opacity when Windows is in light mode
    --autostart <on|off>       apply at sign-in and re-apply after Explorer restarts
barely --settings            open the settings window
barely --status              show what Barely is doing
barely --diagnose            write a report to attach to bug reports
barely --restore             stock taskbar, autostart off
barely --version
```

Examples:

```
barely 0                                  fully clear
barely 30 --tint #000000                  30% black, like a smoked glass
barely 0 --maximized 100                  clear, but solid while a window is maximized
barely 10 --light 50 --border 0           subtle in dark mode, more visible in light mode
```

Keep `barely.exe`, `barelyw.exe` and `barely_tap.dll` together. `barelyw.exe` is the same
program with no console window, used for autostart.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | Failed (the message says why) |
| 2 | Invalid command line |
| 3 | Loaded, but the taskbar layout wasn't recognized (run `--diagnose`) |
| 4 | `barely_tap.dll` missing or modified |
| 5 | Started as administrator (not allowed) |
| 6 | Not Windows 11 |

## How it works

The Windows 11 taskbar is drawn with XAML. Its background is a `Rectangle` named
`BackgroundFill`, plus a `BackgroundStroke` for the top border line, both under
`Taskbar.TaskbarBackground`. Windows has a supported developer API,
`InitializeXamlDiagnosticsEx`, that lets a tool load a DLL into a XAML app to inspect
its visual tree.

1. `barely.exe` saves your settings to `HKCU\Software\Barely`, checks the DLL and the
   target process (see [Security](#security)), and calls `InitializeXamlDiagnosticsEx`
   on Explorer.
2. `barely_tap.dll` finds those rectangles and sets their `Opacity` (and a tint brush if
   you chose one). Property-changed callbacks re-apply the values if Windows resets them.
3. One thread in the DLL sleeps until something happens: a settings change, a light/dark
   switch, or (only with `--maximized`) a window being maximized or focused. There is no
   polling. Changing a setting is just a registry write; the DLL picks it up live.
4. The DLL reports back through a volatile registry key: how many taskbar elements it's
   styling and which settings it saw. That's how `barely` can tell you when something's
   wrong instead of claiming success.

### Autostart and auto-repair

`--autostart on` sets up two things, neither of which stays running:

- A **Run entry** that applies Barely when you sign in.
- A **scheduled task**, "Barely Auto-Repair", that starts `barelyw.exe --ensure` once a
  minute. If Barely is already in Explorer it exits within milliseconds; if Explorer
  restarted, it loads Barely again. Windows has no "Explorer started" event a normal user
  can subscribe to, so this is the lightest reliable option.

## Security

Barely loads code into Explorer, so it is careful about what it loads and where.

| Protection | What it prevents |
|---|---|
| SHA-256 of `barely_tap.dll` is baked into `barely.exe` at build time and checked before loading | A swapped or modified DLL being loaded into Explorer |
| The DLL is held open with no write/delete sharing from the check until Explorer has loaded it | Swapping the file between the check and the load (TOCTOU) |
| The DLL is verified before any setting is written | A tampered install changing anything |
| Target must be `%SystemRoot%\explorer.exe` in your own session | A fake window named `Shell_TrayWnd` tricking Barely |
| The DLL refuses to activate in any process except `%SystemRoot%\explorer.exe` | The DLL being reused as a payload |
| Refuses to run as administrator | Running with more rights than needed |
| `SetDefaultDllDirectories(System32)`, `/DEPENDENTLOADFLAG:0x800`, and `schtasks.exe` started by full System32 path | DLL and executable search-order hijacking |
| Autostart paths are quoted; the scheduled task runs as you, with least privilege | Unquoted-path hijacking, privilege escalation |
| Strict argument parsing; the DLL's only inputs are clamped DWORDs | Malformed input |
| No exception can escape into Explorer | Barely crashing your shell |
| `/sdl`, Control Flow Guard, ASLR, DEP, CET shadow stack, static CRT; MSVC `/analyze` clean; CodeQL in CI | Memory-corruption exploits and common bug classes |

See [SECURITY.md](SECURITY.md) for the threat model and how to report a problem.

## Tested Windows builds

| Build | Version | Status |
|---|---|---|
| 26200 | Windows 11 25H2 | Taskbar recognized; automated tests pass |

Other Windows 11 builds use the same taskbar XAML and will most likely work. Barely
shows a note on untested builds. Please report results with `barely --status`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| "couldn't find the taskbar background" | A Windows update changed the taskbar. Run `barely --diagnose` and open an issue with the report. |
| "a different version of Barely is still loaded" | You updated Barely. Sign out and back in, or restart Explorer. |
| "your settings aren't reaching it" | Run Barely from a normal terminal or Win+R, not from inside a sandboxed app. |
| Taskbar went back to normal | Explorer restarted. Run `barely`, or turn on `--autostart on`. |
| Antivirus warning | Barely loads a DLL into Explorer, which some heuristics flag. Build it yourself from source, or check the release checksums. |

## Build

You need the MSVC compiler and the Windows SDK (headers and libs). Either:

- **Installed toolchain:** Visual Studio Build Tools with the C++ workload. `build.cmd`
  finds it through `vswhere`.
- **Environment variables:** if `cl` is on `PATH` and `INCLUDE`/`LIB` point at the MSVC
  and Windows SDK folders, `build.cmd` uses them directly. This works with the SDK
  unpacked from the `Microsoft.Windows.SDK.CPP` NuGet packages.

```
build.cmd                 build into build\
build.cmd analyze         build with the MSVC static analyzer
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for development notes.

## Limitations

- Per-monitor settings aren't supported yet: all taskbars share one setting.
- The "maximized" mode follows the active window, not each monitor separately.
- Windows 11 only. The Windows 10 taskbar is not XAML.
- Release binaries aren't code-signed yet, so SmartScreen may warn on first run.

## Undo completely

Run `barely --restore`, then sign out (or restart Explorer) to unload the DLL. Delete
the folder, `HKCU\Software\Barely` and `%LOCALAPPDATA%\Barely`.

## Project files

| Path | Purpose |
|---|---|
| `src/barely.cpp` | CLI, settings window, autostart, and loading the DLL into Explorer |
| `src/barely_tap.cpp` | DLL that runs inside Explorer and styles the taskbar |
| `src/barely_shared.h`, `src/version.h` | Constants shared by both |
| `src/barely.rc`, `src/barely.manifest` | Version info (author, copyright), DPI awareness |
| `tools/embed_hash.ps1` | Build step that records the DLL's SHA-256 |
| `tests/run-tests.ps1` | Automated tests |
| `.github/workflows/` | CI build and tests, CodeQL, releases |
| [CHANGELOG.md](CHANGELOG.md) | Release notes per version |
| [devlog/](devlog/) | Development log (update notes) |

## Author and license

Barely was designed and created solely by **AayuShen**.

Released under the [MIT License](LICENSE). You're free to use, modify and share it,
as long as the copyright notice crediting AayuShen is kept.
