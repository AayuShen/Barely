// Barely — a transparent Windows 11 taskbar with no background process.
// Created by AayuShen. Copyright (c) 2026 AayuShen. Licensed under the MIT License.
#pragma once
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include "version.h"

// Shared between barely.exe (injector) and barely_tap.dll (runs inside explorer.exe).

inline constexpr wchar_t kVersion[] = L"" BARELY_VERSION_STR;
inline constexpr wchar_t kRepoUrl[] = L"https://github.com/AayuShen/Barely-There-Taskbar";

// {622816ED-EA44-44EB-8CC0-8AB2F581F3CC}
inline constexpr CLSID CLSID_BarelyTap = {
    0x622816ed, 0xea44, 0x44eb, {0x8c, 0xc0, 0x8a, 0xb2, 0xf5, 0x81, 0xf3, 0xcc}};

inline constexpr wchar_t kTapDllName[] = L"barely_tap.dll";

// Settings live in HKCU\Software\Barely. The DLL watches this key, so any change is
// applied to the taskbar live. A missing Opacity value means "stock taskbar".
inline constexpr wchar_t kRegKey[] = L"Software\\Barely";
// Volatile key the DLL writes so barely.exe can tell what happened inside Explorer.
inline constexpr wchar_t kRegRuntimeKey[] = L"Software\\Barely\\Runtime";

namespace reg {
inline constexpr wchar_t kOpacity[] = L"Opacity";             // 0-100
inline constexpr wchar_t kStroke[] = L"StrokeOpacity";        // 0-100, missing = same as opacity
inline constexpr wchar_t kMaximized[] = L"MaximizedOpacity";  // 0-100, missing = off
inline constexpr wchar_t kLight[] = L"LightOpacity";          // 0-100, missing = same as opacity
inline constexpr wchar_t kTint[] = L"Tint";                   // 0xRRGGBB, missing = no tint
inline constexpr wchar_t kDiagRequest[] = L"DiagnoseRequest";
inline constexpr wchar_t kStamp[] = L"Stamp";  // changes on every apply; echoed as StampSeen
// Runtime key
inline constexpr wchar_t kStampSeen[] = L"StampSeen";
inline constexpr wchar_t kTargets[] = L"Targets";  // taskbar elements being styled
inline constexpr wchar_t kPid[] = L"Pid";          // explorer.exe the DLL lives in
inline constexpr wchar_t kTapVersion[] = L"Version";
inline constexpr wchar_t kDiagDone[] = L"DiagnoseDone";
}  // namespace reg

constexpr int kUnset = -1;

struct Settings {
    int opacity = kUnset;
    int stroke = kUnset;
    int maximized = kUnset;
    int light = kUnset;
    int tint = kUnset;
};

inline bool ReadDword(const wchar_t* key, const wchar_t* name, DWORD& out) {
    DWORD size = sizeof(out);
    return RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &out, &size) ==
           ERROR_SUCCESS;
}

inline int ReadClamped(const wchar_t* name, DWORD max) {
    DWORD v = 0;
    if (!ReadDword(kRegKey, name, v)) return kUnset;
    return static_cast<int>(v > max ? max : v);
}

inline Settings ReadSettings() {
    Settings s;
    s.opacity = ReadClamped(reg::kOpacity, 100);
    s.stroke = ReadClamped(reg::kStroke, 100);
    s.maximized = ReadClamped(reg::kMaximized, 100);
    s.light = ReadClamped(reg::kLight, 100);
    s.tint = ReadClamped(reg::kTint, 0xFFFFFF);
    return s;
}

// %LOCALAPPDATA%\Barely\diagnose.txt, resolved through the shell's known-folder API rather
// than the (easily altered) environment variable.
inline bool DiagnosePath(wchar_t (&path)[MAX_PATH]) {
    PWSTR base = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &base)))
        return false;
    bool const fits = lstrlenW(base) < MAX_PATH - 24;
    if (fits) lstrcpyW(path, base);
    CoTaskMemFree(base);
    if (!fits) return false;
    lstrcatW(path, L"\\Barely");
    CreateDirectoryW(path, nullptr);
    lstrcatW(path, L"\\diagnose.txt");
    return true;
}

// True if this process's image is %SystemRoot%\explorer.exe.
inline bool IsSystemExplorer(HANDLE process) {
    wchar_t expected[MAX_PATH], actual[MAX_PATH];
    UINT n = GetWindowsDirectoryW(expected, MAX_PATH);
    if (!n || n > MAX_PATH - 14) return false;
    lstrcatW(expected, L"\\explorer.exe");
    DWORD size = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, actual, &size)) return false;
    return lstrcmpiW(expected, actual) == 0;
}
