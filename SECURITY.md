# Security

Barely is created and maintained solely by **AayuShen**.

## Threat model

Barely loads its own DLL into `explorer.exe`, the Windows shell. The main risk is
that something else gets loaded or executed in its place. Barely defends against:

- **A tampered or replaced `barely_tap.dll`.** `barely.exe` refuses to load any DLL
  whose SHA-256 differs from the one recorded at build time. It keeps the file locked
  against writes and deletes until Explorer has loaded it.
- **A spoofed taskbar.** Before loading anything, Barely confirms that the process
  owning `Shell_TrayWnd` is `%SystemRoot%\explorer.exe` in the caller's session.
- **Reuse of the DLL elsewhere.** `barely_tap.dll` does nothing unless its host process
  is `%SystemRoot%\explorer.exe`.
- **DLL search-order hijacking.** Executables load system DLLs from System32 only.
- **Privilege misuse.** Barely refuses to run elevated. It writes only to `HKCU`.

Out of scope: an attacker who already runs code as you can change Explorer directly
and does not need Barely. Barely makes no network connections and collects no data.

## What Barely changes on your system

- `HKCU\Software\Barely\Opacity` (DWORD)
- `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Barely`, only with `--autostart on`
- The `Opacity` of two taskbar XAML elements, in memory only

## Reporting a vulnerability

Please report security problems privately to AayuShen through GitHub's
**Security → Report a vulnerability** on this repository, not in a public issue.
