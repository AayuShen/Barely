# Security

Barely is created and maintained solely by **AayuShen**.

## Supported versions

Only the latest release gets security fixes.

## Threat model

Barely loads its own DLL into `explorer.exe`, the Windows shell. The main risk is
that something else gets loaded or executed in its place. Barely defends against:

- **A tampered or replaced `barely_tap.dll`.** `barely.exe` refuses to load any DLL
  whose SHA-256 differs from the one recorded at build time. It checks this before
  writing any setting, and keeps the file locked against writes and deletes until
  Explorer has loaded it.
- **A spoofed taskbar.** Before loading anything, Barely confirms that the process
  owning `Shell_TrayWnd` is `%SystemRoot%\explorer.exe` in the caller's session.
- **Reuse of the DLL elsewhere.** `barely_tap.dll` does nothing unless its host process
  is `%SystemRoot%\explorer.exe`.
- **Search-order hijacking.** Executables load system DLLs from System32 only, and start
  `schtasks.exe` by its full System32 path.
- **Privilege misuse.** Barely refuses to run elevated. It writes only to `HKCU` and your
  own `%LOCALAPPDATA%`. The auto-repair task runs as you, with least privilege, only
  while you're signed in.
- **Malformed input.** The command line is parsed strictly; the DLL's only inputs are
  registry DWORDs clamped to valid ranges.
- **Crashing the shell.** No exception can leave the DLL. Window-event hooks are only
  installed when `--maximized` is used, and only for the foreground process.

Out of scope: an attacker who already runs code as you can change Explorer directly
and does not need Barely. Barely makes no network connections and collects no data.

## Verifying a release

Each release lists SHA-256 checksums, built by GitHub Actions from the tagged source.
Check a download with:

```
Get-FileHash .\Barely-v1.1.0-win-x64.zip -Algorithm SHA256
```

Release binaries are not code-signed yet.

## What Barely changes on your system

- `HKCU\Software\Barely`: your settings (DWORDs) and a volatile `Runtime` subkey the DLL
  uses to report its state
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Barely`, only with `--autostart on`
- Scheduled task "Barely Auto-Repair", only with `--autostart on`
- `%LOCALAPPDATA%\Barely\diagnose.txt`, only when you run `--diagnose`
- The `Opacity` (and with a tint, the `Fill`) of two taskbar XAML elements, in memory only

`barely --restore` removes the settings, the Run entry and the task.

## Reporting a vulnerability

Please report security problems privately to AayuShen through GitHub's
**Security → Report a vulnerability** on this repository, not in a public issue.
