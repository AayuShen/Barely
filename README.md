# Barely

A transparent taskbar for Windows 11, with nothing left running in the background.

[![Build](https://github.com/AayuShen/Barely/actions/workflows/build.yml/badge.svg)](https://github.com/AayuShen/Barely/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/AayuShen/Barely)](https://github.com/AayuShen/Barely/releases/latest)

Most taskbar tools stay running all day to keep re-applying their effect. Barely
doesn't. It loads a tiny DLL into Explorer, styles the taskbar from the inside, and
exits after about a second. No tray icon and no extra process in Task Manager.

## Install

1. Download the latest zip from [Releases](https://github.com/AayuShen/Barely/releases/latest).
2. Extract it somewhere you'll keep it.
3. Open a terminal in that folder and run `.\barely.exe`.

That's it: your taskbar is now clear. For sliders instead of commands, run `.\barely.exe --settings`.

## Usage

```
barely                     fully clear (or your saved settings)
barely 30                  30% opacity
barely 30 --tint #000000   smoky black glass
barely 0 --maximized 100   clear, but solid when a window is maximized
barely --autostart on      apply at sign-in, and fix itself after Explorer restarts
barely --restore           back to the normal taskbar
```

Run `barely --help` for every option, and `barely --status` to see what it's doing.

## Good to know

- Windows 11 only. Verified on build 26200; other builds should work too.
- If Explorer restarts, the effect goes away until you run `barely` again, unless
  autostart is on.
- After updating Barely, sign out and back in once so Explorer picks up the new version.
- It doesn't need admin rights, and it refuses to run as admin.

## If something goes wrong

Barely prints what happened and exits with a code, so scripts can check it too.

| Code | What you'll see | What it means / what to do |
|---|---|---|
| 0 | `Taskbar: opacity 0%, ...` | It worked. |
| 1 | "a different version of Barely is still loaded" | You updated Barely. Sign out and back in, then run it again. |
| 1 | "your settings aren't reaching it" | You ran it from inside a sandboxed app. Use a normal terminal or Win+R. |
| 1 | "The taskbar isn't running" | Explorer hasn't started yet. Wait a few seconds and try again. |
| 2 | The help text | Something in the command was mistyped. Check `barely --help`. |
| 3 | "couldn't find the taskbar background" | A Windows update probably changed the taskbar. Run `barely --diagnose` and [open an issue](https://github.com/AayuShen/Barely/issues) with the report. |
| 4 | "barely_tap.dll is missing or has been modified" | The files are damaged or were changed. Download a fresh copy from Releases. |
| 5 | "Run Barely as a normal user" | You started it as administrator. Run it normally. |
| 6 | "Barely needs Windows 11" | Windows 10 isn't supported. |

For anything else, run `barely --diagnose` and
[open an issue](https://github.com/AayuShen/Barely/issues) with the report.

## How it works

The Windows 11 taskbar is drawn with XAML. Barely uses a Windows developer API
(`InitializeXamlDiagnosticsEx`) to load its DLL into Explorer. The DLL finds the
rectangles that paint the taskbar background and changes their opacity and colour.
After that it just sleeps until you change a setting.

Because it loads code into Explorer, Barely checks the DLL's SHA-256 hash and makes sure
it's talking to the real `explorer.exe` before loading anything. Details are in
[SECURITY.md](SECURITY.md).

## Building from source

You need the MSVC compiler and the Windows SDK. Then run:

```
build.cmd
powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for more, and [CHANGELOG.md](CHANGELOG.md) for
what's new.

## License

MIT. See [LICENSE](LICENSE).

---

Made by **AayuShen**. If you like it, give it a star :)
