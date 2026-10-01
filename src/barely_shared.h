// Barely — a transparent Windows 11 taskbar with no background process.
// Created by AayuShen. Copyright (c) 2026 AayuShen. All rights reserved.
#pragma once
#include <windows.h>

// Shared between barely.exe (injector) and barely_tap.dll (runs inside explorer.exe).

#define BARELY_VERSION_STR "1.0.0"
inline constexpr wchar_t kVersion[] = L"" BARELY_VERSION_STR;

// {622816ED-EA44-44EB-8CC0-8AB2F581F3CC}
inline constexpr CLSID CLSID_BarelyTap = {
    0x622816ed, 0xea44, 0x44eb, {0x8c, 0xc0, 0x8a, 0xb2, 0xf5, 0x81, 0xf3, 0xcc}};

// HKCU\Software\Barely\Opacity (DWORD, 0-100). Missing value = stock taskbar.
// The DLL watches this key, so changing it updates the taskbar live.
inline constexpr wchar_t kRegKey[] = L"Software\\Barely";
inline constexpr wchar_t kRegOpacity[] = L"Opacity";

inline constexpr wchar_t kTapDllName[] = L"barely_tap.dll";

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
