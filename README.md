# Barely

**A transparent Windows 11 taskbar with no background process.**

> Barely was created solely by **AayuShen**.
> Copyright (c) 2026 AayuShen. Released under the [MIT License](LICENSE).

`barely.exe` runs for about a second and exits. It loads a small DLL into
`explorer.exe`, which sets the opacity of the taskbar's background. The DLL stays
idle inside Explorer (one thread waiting on a registry event, no polling) until
Explorer restarts. No tray icon, no service, no extra process in Task Manager.

## Usage

```
barely                 apply the saved opacity (0 = fully clear if none is saved)
barely 30              30% opaque background (0-100)
barely --autostart on  apply at every logon (runs barelyw.exe once, then it exits)
barely --restore       stock taskbar again, and disable autostart
barely --version
```

Keep `barely.exe`, `barelyw.exe` and `barely_tap.dll` in the same folder.
`barelyw.exe` is the same program with no console window, used for autostart.

## How it works

The Windows 11 taskbar is drawn with XAML. Its background is a `Rectangle` named
`BackgroundFill`, plus a `BackgroundStroke` for the top border line, both under
`Taskbar.TaskbarBackground`. Windows has a supported developer API,
`InitializeXamlDiagnosticsEx`, that lets a tool load a DLL into a XAML app to inspect
its visual tree.

1. `barely.exe` saves your opacity to `HKCU\Software\Barely\Opacity`, checks the DLL
   and the target process (see [Security](#security)), then calls
   `InitializeXamlDiagnosticsEx` on Explorer.
2. `barely_tap.dll` subscribes to visual-tree changes, finds those two rectangles, and
   sets their `Opacity`. It also registers a property-changed callback, so if Windows
   resets the value (for example on a theme change), the DLL sets it again right away.
3. The DLL watches the registry key, so running `barely 40` later updates the taskbar
   live without loading anything again.

Because only the taskbar's own XAML is changed, it stays transparent when you open
Start, maximize windows or switch apps. The older `SetWindowCompositionAttribute`
trick does not, which is why tools that use it have to keep running.

## Security

Barely loads code into Explorer, so it is careful about what it loads and where.

| Protection | What it prevents |
|---|---|
| SHA-256 of `barely_tap.dll` is baked into `barely.exe` at build time and checked before loading | A swapped or modified DLL being loaded into Explorer |
| The DLL is held open with no write/delete sharing from the check until Explorer has loaded it | Swapping the file between the check and the load (TOCTOU) |
| Target must be `%SystemRoot%\explorer.exe` in your own session | A fake window named `Shell_TrayWnd` tricking Barely into loading the DLL into another process |
| The DLL refuses to activate in any process except `%SystemRoot%\explorer.exe` | The DLL being reused as a payload in other processes |
| Refuses to run as administrator | Running with more rights than it needs (normal user rights are enough) |
| `SetDefaultDllDirectories(System32)` plus `/DEPENDENTLOADFLAG:0x800` | DLL hijacking through files planted next to the exe |
| Autostart path is quoted in the Run key | Unquoted-path hijacking |
| Strict argument parsing; the DLL's only input is one DWORD, clamped to 0-100 | Malformed input |
| No exception can escape into Explorer | Barely crashing your shell |
| Built with `/sdl`, Control Flow Guard, ASLR, DEP, CET shadow stack and the static CRT | Common memory-corruption exploits, and depending on a separate C runtime install |
| Single-instance mutex | Two copies loading the DLL at the same time |

See [SECURITY.md](SECURITY.md) for the threat model and how to report a problem.

## Build

You need the MSVC compiler and the Windows SDK (headers and libs). Either:

- **Installed toolchain:** Visual Studio Build Tools with the C++ workload. `build.cmd`
  finds it through `vswhere`.
- **Environment variables:** if `cl` is on `PATH` and `INCLUDE`/`LIB` point at the MSVC
  and Windows SDK folders, `build.cmd` uses them directly. This works with the SDK
  unpacked from the `Microsoft.Windows.SDK.CPP` NuGet packages.

Then run:

```
build.cmd
```

The build creates `build\barely.exe`, `build\barelyw.exe` and `build\barely_tap.dll`.
After you change `barely_tap.cpp` you must rebuild everything, because the DLL hash is
embedded in the executables.

## Limitations

- **Restarting Explorer** (or a crash) removes the DLL. Run `barely` again. Autostart
  covers logons.
- **Rebuilding while it's loaded:** Explorer locks `barely_tap.dll`, so restart Explorer
  before you overwrite it.
- **Windows updates** could rename the taskbar's XAML elements. If that happens, the
  code to update is the type and name checks in `src/barely_tap.cpp`.
- Windows 11 only. The Windows 10 taskbar is not XAML.

## Undo completely

Run `barely --restore`, then restart Explorer (or sign out) to unload the DLL.
Delete the folder and `HKCU\Software\Barely`.

## Project files

| Path | Purpose |
|---|---|
| `src/barely.cpp` | CLI: saves settings, verifies, loads the DLL into Explorer |
| `src/barely_tap.cpp` | DLL that runs inside Explorer and changes the taskbar |
| `src/barely_shared.h` | Constants shared by both |
| `src/barely.rc` | Version info (author, copyright) embedded in the binaries |
| `tools/embed_hash.ps1` | Build step that records the DLL's SHA-256 |
| [CHANGELOG.md](CHANGELOG.md) | Release notes per version |
| [devlog/](devlog/) | Development log entries |

## Author and license

Barely was designed and created solely by **AayuShen**.

Released under the [MIT License](LICENSE). You're free to use, modify and share it,
as long as the copyright notice crediting AayuShen is kept.
