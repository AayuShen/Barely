# Contributing to Barely

Barely was created by **AayuShen** and is released under the [MIT License](LICENSE).
Contributions are welcome. By contributing, you agree that your work is released under
the same license.

## Ground rules

- **Nothing stays running.** Barely's promise is no background process. New features
  must work from inside Explorer (`src/barely_tap.cpp`) or as one-shot commands.
- **The DLL runs inside the Windows shell.** Never let an exception escape, never block
  the UI thread, and never poll. Wait on events instead.
- **Security checks stay in place.** See [SECURITY.md](SECURITY.md).

## Building

See the Build section of the [README](README.md). For a static analysis pass:

```
build.cmd analyze
```

For a trace log from inside Explorer (`%LOCALAPPDATA%\Barely\trace.log`):

```
set BARELY_CFLAGS=/DBARELY_TRACE
build.cmd
```

## Testing

1. `powershell -ExecutionPolicy Bypass -File tests\run-tests.ps1` runs the automated
   tests. CI runs them on every push.
2. Changes to the DLL also need the manual checks in [docs/TESTING.md](docs/TESTING.md)
   on a real Windows 11 machine.

Explorer locks a loaded `barely_tap.dll`. `build.cmd` moves the old copy aside, but the
new DLL is only used after Explorer restarts.

## Releasing (maintainer)

1. Bump `src/version.h` and the version in `src/barely.manifest`.
2. Move the "Unreleased" notes in `CHANGELOG.md` under the new version, and add a devlog
   entry in `devlog/`.
3. Tag and push: `git tag v1.2.0 && git push origin v1.2.0`. The Release workflow builds,
   tests and publishes the release.
