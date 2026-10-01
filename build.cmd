@echo off
rem Barely - created by AayuShen.
rem Builds build\barely.exe, build\barelyw.exe and build\barely_tap.dll with MSVC.
rem Uses cl/INCLUDE/LIB from the environment if set; otherwise falls back to vcvars64.bat.
rem   build.cmd           normal build
rem   build.cmd analyze   also run the MSVC static analyzer (/analyze)
setlocal

where cl >nul 2>nul
if errorlevel 1 goto :vcvars
if not defined INCLUDE goto :vcvars
goto :build

:vcvars
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo MSVC not found. See README.md.
    exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
cd /d "%~dp0"
if not exist build mkdir build

rem Explorer locks a loaded barely_tap.dll. A locked file can still be renamed, so move it
rem aside; the old copy stays in use until Explorer restarts.
if exist build\barely_tap.dll (
    del /q build\barely_tap.dll >nul 2>nul
    if exist build\barely_tap.dll (
        if not exist build\stale mkdir build\stale
        move /y build\barely_tap.dll "build\stale\barely_tap.%RANDOM%%RANDOM%.dll" >nul || exit /b 1
        echo Note: the previous barely_tap.dll is still loaded in Explorer. Restart Explorer to use the new build.
    )
)

rem Hardening: /sdl extra checks, /guard:cf Control Flow Guard, ASLR/DEP/CET at link time,
rem and static imports resolved from System32 only.
set "CFLAGS=/nologo /std:c++20 /EHsc /O2 /MT /W4 /permissive- /bigobj /sdl /guard:cf /DUNICODE /D_UNICODE"
rem Extra compiler flags for development, e.g. set BARELY_CFLAGS=/DBARELY_TRACE
if defined BARELY_CFLAGS set "CFLAGS=%CFLAGS% %BARELY_CFLAGS%"
if /i "%~1"=="analyze" set "CFLAGS=%CFLAGS% /analyze /analyze:external- /external:anglebrackets /external:W0"
set "LFLAGS=/DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /CETCOMPAT /guard:cf /DEPENDENTLOADFLAG:0x800"

rc /nologo /DBARELY_DLL /fo build\barely_tap.res src\barely.rc || exit /b 1
rc /nologo /fo build\barely.res src\barely.rc || exit /b 1

cl %CFLAGS% /LD src\barely_tap.cpp build\barely_tap.res /Fobuild\ /Fe:build\barely_tap.dll ^
    /link %LFLAGS% /DEF:src\barely_tap.def ole32.lib oleaut32.lib advapi32.lib user32.lib dwmapi.lib shell32.lib windowsapp.lib || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File tools\embed_hash.ps1 -Dll build\barely_tap.dll -Out build\tap_hash.h || exit /b 1

set "EXELIBS=user32.lib advapi32.lib bcrypt.lib comctl32.lib comdlg32.lib gdi32.lib shell32.lib ole32.lib"

cl %CFLAGS% /Ibuild src\barely.cpp build\barely.res /Fobuild\barely.obj /Fe:build\barely.exe ^
    /link %LFLAGS% /MANIFEST:NO /SUBSYSTEM:CONSOLE %EXELIBS% || exit /b 1

cl %CFLAGS% /Ibuild /DBARELY_GUI src\barely.cpp build\barely.res /Fobuild\barelyw.obj /Fe:build\barelyw.exe ^
    /link %LFLAGS% /MANIFEST:NO /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup %EXELIBS% || exit /b 1

del /q build\*.obj build\*.exp build\*.lib build\*.res 2>nul
echo.
echo Built: build\barely.exe  build\barelyw.exe  build\barely_tap.dll
