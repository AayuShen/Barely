// Barely — a transparent Windows 11 taskbar with no background process.
// Created by AayuShen. Copyright (c) 2026 AayuShen. Licensed under the MIT License.
//
// barely_tap.dll — loaded into explorer.exe through the XAML diagnostics API.
//
// It watches the taskbar's XAML tree, finds the Rectangles that paint the taskbar
// background (Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill/#BackgroundStroke)
// and sets their Opacity (and optionally a tint brush). Property-changed callbacks re-apply
// the values if Windows resets them.
//
// One control thread sleeps until something happens: a settings change in the registry,
// a light/dark theme switch, or (only when "maximized" mode is on) a foreground/maximize
// window event. There is no polling.
//
// Security: the DLL refuses to activate in any process other than %SystemRoot%\explorer.exe,
// reads only clamped DWORDs as input, and never lets an exception reach explorer.

#include "barely_shared.h"

#include <unknwn.h>
#include <dwmapi.h>
#include <ocidl.h>
#include <xamlom.h>
#undef GetCurrentTime  // winbase.h macro clashes with C++/WinRT

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxm = wux::Media;
namespace wuxs = wux::Shapes;
using winrt::Windows::UI::Core::CoreDispatcher;
using winrt::Windows::UI::Core::CoreDispatcherPriority;

namespace {

struct Target {
    winrt::weak_ref<wux::UIElement> element;
    CoreDispatcher dispatcher{nullptr};
    bool isStroke = false;
    // Touched only on the element's UI thread.
    int64_t opacityToken = 0;
    int64_t fillToken = 0;
    bool tinted = false;
};

// Effective values computed by the control thread. Opacity < 0 = stock look, tint < 0 = none.
std::atomic<double> g_fillOpacity{-1.0};
std::atomic<double> g_strokeOpacity{-1.0};
std::atomic<int> g_tint{kUnset};

winrt::com_ptr<IXamlDiagnostics> g_diag;
std::once_flag g_started;

std::mutex g_lock;  // guards everything below
std::unordered_map<InstanceHandle, InstanceHandle> g_parent;
std::unordered_set<InstanceHandle> g_backgrounds;
std::vector<std::shared_ptr<Target>> g_targets;
// A bounded sample of the visual tree, for `barely --diagnose`.
constexpr size_t kMaxSeen = 400;
std::set<std::wstring> g_seenTypes;
std::set<std::wstring> g_seenRectNames;

#ifdef BARELY_TRACE
void Trace(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = sprintf_s(buf, "[%lu] ", GetCurrentThreadId());
    n += vsprintf_s(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);
    buf[n++] = '\n';
    wchar_t path[MAX_PATH] = {};
    if (!DiagnosePath(path)) return;
    lstrcpyW(wcsrchr(path, L'\\') + 1, L"trace.log");
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, buf, n, &w, nullptr);
    CloseHandle(f);
}
#else
#define Trace(...) ((void)0)
#endif

// XAML may store opacity with less precision than a double, so compare with a tolerance.
bool Same(double a, double b) { return a < 0 ? b < 0 : (b >= 0 && (a - b < 0.001 && b - a < 0.001)); }

// ---- Applying values (UI thread) ----------------------------------------------------------

winrt::Windows::UI::Color TintColor(int rgb) {
    return {255, static_cast<uint8_t>(rgb >> 16), static_cast<uint8_t>(rgb >> 8),
            static_cast<uint8_t>(rgb)};
}

bool HasTint(wuxs::Shape const& shape, int rgb) {
    auto brush = shape.Fill().try_as<wuxm::SolidColorBrush>();
    return brush && brush.Color() == TintColor(rgb);
}

void ApplyOpacity(wux::UIElement const& el, Target& t, double op) {
    auto const prop = wux::UIElement::OpacityProperty();
    if (op < 0) {
        if (t.opacityToken) {
            el.UnregisterPropertyChangedCallback(prop, t.opacityToken);
            t.opacityToken = 0;
        }
        el.ClearValue(prop);
        return;
    }
    if (!t.opacityToken) {
        bool const isStroke = t.isStroke;
        t.opacityToken = el.RegisterPropertyChangedCallback(
            prop, [isStroke](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
                double const want = (isStroke ? g_strokeOpacity : g_fillOpacity).load();
                if (want < 0) return;
                auto e = sender.as<wux::UIElement>();
                if (!Same(e.Opacity(), want)) e.Opacity(want);
            });
    }
    el.Opacity(op);
}

void ApplyTint(wuxs::Shape const& shape, Target& t, int rgb) {
    auto const prop = wuxs::Shape::FillProperty();
    if (rgb < 0) {
        if (t.fillToken) {
            shape.UnregisterPropertyChangedCallback(prop, t.fillToken);
            t.fillToken = 0;
        }
        if (t.tinted) {
            shape.ClearValue(prop);
            t.tinted = false;
        }
        return;
    }
    if (!t.fillToken) {
        t.fillToken = shape.RegisterPropertyChangedCallback(
            prop, [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
                int const want = g_tint.load();
                if (want < 0 || g_fillOpacity.load() < 0) return;
                auto s = sender.as<wuxs::Shape>();
                if (!HasTint(s, want)) s.Fill(wuxm::SolidColorBrush(TintColor(want)));
            });
    }
    if (!HasTint(shape, rgb)) shape.Fill(wuxm::SolidColorBrush(TintColor(rgb)));
    t.tinted = true;
}

void Apply(wux::UIElement const& el, Target& t) {
    double const op = (t.isStroke ? g_strokeOpacity : g_fillOpacity).load();
    ApplyOpacity(el, t, op);
    if (!t.isStroke) ApplyTint(el.as<wuxs::Shape>(), t, op < 0 ? kUnset : g_tint.load());
}

void ReapplyAll() {
    std::lock_guard lock(g_lock);
    Trace("reapply %zu targets", g_targets.size());
    for (auto const& t : g_targets) {
        try {
            t->dispatcher.RunAsync(CoreDispatcherPriority::Normal, [t] {
                try {
                    if (auto el = t->element.get()) Apply(el, *t);
                } catch (...) {
                }
            });
        } catch (...) {
        }
    }
}

// Lets barely.exe confirm the taskbar was recognized (and which DLL version is loaded).
void PublishState(DWORD targets) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegRuntimeKey, 0, nullptr, REG_OPTION_VOLATILE,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    DWORD const pid = GetCurrentProcessId();
    RegSetValueExW(key, reg::kTargets, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&targets),
                   sizeof(targets));
    RegSetValueExW(key, reg::kPid, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&pid), sizeof(pid));
    RegSetValueExW(key, reg::kTapVersion, 0, REG_SZ, reinterpret_cast<const BYTE*>(kVersion),
                   sizeof(kVersion));
    RegCloseKey(key);
}

// ---- Visual tree watching -----------------------------------------------------------------

bool UnderTaskbarBackground(InstanceHandle h) {
    for (int depth = 0; depth < 4 && h; ++depth) {
        if (g_backgrounds.contains(h)) return true;
        auto it = g_parent.find(h);
        if (it == g_parent.end()) return false;
        h = it->second;
    }
    return false;
}

struct Watcher : winrt::implements<Watcher, IVisualTreeServiceCallback2, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation relation, VisualElement element,
                                                 VisualMutationType mutation) override try {
        std::lock_guard lock(g_lock);
        if (mutation == Remove) {
            g_parent.erase(element.Handle);
            g_backgrounds.erase(element.Handle);
            return S_OK;
        }

        g_parent[element.Handle] = relation.Parent;
        std::wstring_view const type = element.Type ? element.Type : L"";
        std::wstring_view const name = element.Name ? element.Name : L"";
        if (g_seenTypes.size() < kMaxSeen) g_seenTypes.emplace(type);

        if (type == L"Taskbar.TaskbarBackground") {
            g_backgrounds.insert(element.Handle);
            return S_OK;
        }
        if (type != L"Windows.UI.Xaml.Shapes.Rectangle") return S_OK;
        if (!name.empty() && g_seenRectNames.size() < kMaxSeen) g_seenRectNames.emplace(name);
        if ((name != L"BackgroundFill" && name != L"BackgroundStroke") ||
            !UnderTaskbarBackground(relation.Parent))
            return S_OK;

        winrt::com_ptr<::IInspectable> insp;
        winrt::check_hresult(g_diag->GetIInspectableFromHandle(element.Handle, insp.put()));
        wux::UIElement el{nullptr};
        winrt::check_hresult(
            insp->QueryInterface(winrt::guid_of<wux::UIElement>(), winrt::put_abi(el)));

        // Drop targets whose taskbar is gone (we're on a UI thread, so releasing is safe).
        std::erase_if(g_targets, [](auto const& t) { return !t->element.get(); });

        auto t = std::make_shared<Target>();
        t->element = winrt::make_weak(el);
        t->dispatcher = el.Dispatcher();
        t->isStroke = name == L"BackgroundStroke";
        g_targets.push_back(t);
        Apply(el, *t);
        PublishState(static_cast<DWORD>(g_targets.size()));
        return S_OK;
    } catch (...) {
        return S_OK;  // never let an exception escape into explorer
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState,
                                                    LPCWSTR) override {
        return S_OK;
    }
};

winrt::com_ptr<Watcher> g_watcher;

// AdviseVisualTreeChange hangs if called from the thread that delivered SetSite.
DWORD WINAPI AdviseThread(LPVOID) {
    try {
        g_diag.as<IVisualTreeService3>()->AdviseVisualTreeChange(g_watcher.get());
    } catch (...) {
    }
    return 0;
}

// ---- Control thread: settings, theme, maximized windows ------------------------------------
// Everything in this section runs on the control thread only.

Settings g_settings;
bool g_lightTheme = false;
bool g_foregroundMaximized = false;
HWINEVENTHOOK g_foregroundHook = nullptr;
HWINEVENTHOOK g_minimizeHook = nullptr;
HWINEVENTHOOK g_locationHook = nullptr;
DWORD g_locationPid = 0;

bool ReadLightTheme() {
    DWORD v = 0, size = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &v,
                        &size) == ERROR_SUCCESS &&
           v != 0;
}

bool IsForegroundMaximized() {
    HWND fg = GetForegroundWindow();
    if (!fg || !IsWindowVisible(fg) || !IsZoomed(fg)) return false;
    BOOL cloaked = FALSE;
    return !(SUCCEEDED(DwmGetWindowAttribute(fg, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
             cloaked);
}

void Recompute() {
    Settings const& s = g_settings;
    double fill = -1.0, stroke = -1.0;
    int tint = kUnset;
    if (s.opacity >= 0) {
        bool const maxed = s.maximized >= 0 && g_foregroundMaximized;
        int const f = maxed ? s.maximized : (g_lightTheme && s.light >= 0 ? s.light : s.opacity);
        int const st = maxed ? s.maximized : (s.stroke >= 0 ? s.stroke : f);
        fill = f / 100.0;
        stroke = st / 100.0;
        tint = s.tint;
    }
    if (Same(fill, g_fillOpacity) && Same(stroke, g_strokeOpacity) && tint == g_tint) return;
    g_fillOpacity = fill;
    g_strokeOpacity = stroke;
    g_tint = tint;
    ReapplyAll();
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);

// Location changes are only watched for the foreground window's process, which keeps the
// event volume tiny compared to a system-wide hook.
void WatchForegroundProcess() {
    DWORD pid = 0;
    if (HWND fg = GetForegroundWindow()) GetWindowThreadProcessId(fg, &pid);
    if (pid == g_locationPid && g_locationHook) return;
    if (g_locationHook) UnhookWinEvent(g_locationHook);
    g_locationHook = nullptr;
    g_locationPid = pid;
    if (pid)
        g_locationHook = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE,
                                         nullptr, OnWinEvent, pid, 0, WINEVENT_OUTOFCONTEXT);
}

void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD,
                         DWORD) {
    if (event == EVENT_OBJECT_LOCATIONCHANGE &&
        (idObject != OBJID_WINDOW || hwnd != GetForegroundWindow()))
        return;
    if (event == EVENT_SYSTEM_FOREGROUND) WatchForegroundProcess();
    bool const maxed = IsForegroundMaximized();
    if (maxed == g_foregroundMaximized) return;
    g_foregroundMaximized = maxed;
    Recompute();
}

void UpdateHooks() {
    bool const want = g_settings.opacity >= 0 && g_settings.maximized >= 0;
    if (want == (g_foregroundHook != nullptr)) return;
    if (want) {
        g_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                           nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        g_minimizeHook = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
                                         nullptr, OnWinEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
        WatchForegroundProcess();
        g_foregroundMaximized = IsForegroundMaximized();
    } else {
        for (HWINEVENTHOOK* h : {&g_foregroundHook, &g_minimizeHook, &g_locationHook}) {
            if (*h) UnhookWinEvent(*h);
            *h = nullptr;
        }
        g_locationPid = 0;
        g_foregroundMaximized = false;
    }
}

std::wstring Percent(int v, const wchar_t* unsetText) {
    return v < 0 ? std::wstring(unsetText) : std::to_wstring(v) + L"%";
}

void WriteDiagnostics(DWORD request) {
    std::wstring text;
    text += L"Barely " + std::wstring(kVersion) + L" diagnostics (by AayuShen)\r\n";
    text += L"Report issues at " + std::wstring(kRepoUrl) + L"/issues\r\n\r\n";

    OSVERSIONINFOW os{sizeof(os)};
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")))
            fn(&os);
    text += L"Windows build:        " + std::to_wstring(os.dwBuildNumber) + L"\r\n";
    text += L"Explorer pid:         " + std::to_wstring(GetCurrentProcessId()) + L"\r\n";
    text += L"Light theme:          " + std::wstring(g_lightTheme ? L"yes" : L"no") + L"\r\n";
    text += L"Maximized window:     " + std::wstring(g_foregroundMaximized ? L"yes" : L"no") +
            L"\r\n\r\n";
    text += L"Settings: opacity " + Percent(g_settings.opacity, L"stock") + L", border " +
            Percent(g_settings.stroke, L"same") + L", maximized " +
            Percent(g_settings.maximized, L"off") + L", light " +
            Percent(g_settings.light, L"same") + L"\r\n";
    {
        std::lock_guard lock(g_lock);
        text += L"Visual tree elements: " + std::to_wstring(g_parent.size()) + L"\r\n";
        text += L"Taskbar backgrounds:  " + std::to_wstring(g_backgrounds.size()) + L"\r\n";
        text += L"Styled elements:      " + std::to_wstring(g_targets.size()) + L"\r\n\r\n";
        text += L"Rectangle names seen:\r\n";
        for (auto const& n : g_seenRectNames) text += L"  " + n + L"\r\n";
        text += L"\r\nElement types seen:\r\n";
        for (auto const& t : g_seenTypes) text += L"  " + t + L"\r\n";
    }

    wchar_t path[MAX_PATH] = {};
    if (DiagnosePath(path)) {
        int const bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                              nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(),
                            bytes, nullptr, nullptr);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            CloseHandle(file);
        }
    }
    RegSetKeyValueW(HKEY_CURRENT_USER, kRegRuntimeKey, reg::kDiagDone, REG_DWORD, &request,
                    sizeof(request));
}

DWORD WINAPI ControlThread(LPVOID) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    HKEY settingsKey = nullptr, themeKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_NOTIFY | KEY_QUERY_VALUE,
                        nullptr, &settingsKey, nullptr) != ERROR_SUCCESS)
        return 1;
    RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                  0, KEY_NOTIFY, &themeKey);  // optional

    HANDLE events[2] = {CreateEventW(nullptr, FALSE, FALSE, nullptr),
                        CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!events[0] || !events[1]) return 1;
    auto armSettings = [&] {
        return RegNotifyChangeKeyValue(settingsKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, events[0],
                                       TRUE) == ERROR_SUCCESS;
    };
    auto armTheme = [&] {
        if (themeKey)
            RegNotifyChangeKeyValue(themeKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, events[1], TRUE);
    };

    // Read the settings and echo back the stamp barely.exe wrote with them, so it can
    // confirm they actually reached Explorer.
    auto loadSettings = [] {
        g_settings = ReadSettings();
        DWORD stamp = 0;
        if (ReadDword(kRegKey, reg::kStamp, stamp))
            RegSetKeyValueW(HKEY_CURRENT_USER, kRegRuntimeKey, reg::kStampSeen, REG_DWORD, &stamp,
                            sizeof(stamp));
        Trace("settings: opacity=%d stamp=%lu", g_settings.opacity, stamp);
    };

    // Re-arm before reading, so a change made in between is never missed.
    if (!armSettings()) return 1;
    armTheme();
    DWORD lastDiag = 0;
    ReadDword(kRegKey, reg::kDiagRequest, lastDiag);  // only answer requests made from now on
    loadSettings();
    g_lightTheme = ReadLightTheme();
    UpdateHooks();
    Recompute();

    for (;;) {
        DWORD const r = MsgWaitForMultipleObjects(2, events, FALSE, INFINITE, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) {
            if (!armSettings()) break;
            loadSettings();
            UpdateHooks();
            Recompute();
            DWORD diag = 0;
            if (ReadDword(kRegKey, reg::kDiagRequest, diag) && diag != lastDiag) {
                lastDiag = diag;
                WriteDiagnostics(diag);
            }
        } else if (r == WAIT_OBJECT_0 + 1) {
            armTheme();
            g_lightTheme = ReadLightTheme();
            Recompute();
        } else if (r == WAIT_OBJECT_0 + 2) {
            MSG msg;  // delivers the WinEvent callbacks
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        } else {
            break;
        }
    }
    return 0;
}

// ---- COM plumbing -------------------------------------------------------------------------

struct Tap : winrt::implements<Tap, IObjectWithSite, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override try {
        m_site.copy_from(site);
        if (!site) return S_OK;
        std::call_once(g_started, [this] {
            g_diag = m_site.as<IXamlDiagnostics>();
            PublishState(0);
            g_watcher = winrt::make_self<Watcher>();
            if (HANDLE h = CreateThread(nullptr, 0, AdviseThread, nullptr, 0, nullptr))
                CloseHandle(h);
            if (HANDLE h = CreateThread(nullptr, 0, ControlThread, nullptr, 0, nullptr))
                CloseHandle(h);
        });
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        return m_site ? m_site->QueryInterface(riid, ppv) : E_FAIL;
    }

private:
    winrt::com_ptr<IUnknown> m_site;
};

struct Factory : winrt::implements<Factory, IClassFactory> {
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override try {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        return winrt::make_self<Tap>().as<IObjectWithSite>()->QueryInterface(riid, ppv);
    } catch (...) {
        return winrt::to_hresult();
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

}  // namespace

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // Callbacks registered inside explorer point into this DLL, so it must never unload.
        HMODULE self;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&DllMain), &self);
    }
    return TRUE;
}

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID clsid, _In_ REFIID riid,
                                        _Outptr_ LPVOID FAR* ppv) try {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_BarelyTap) return CLASS_E_CLASSNOTAVAILABLE;
    // Only ever act on the real Windows shell, never in an arbitrary host process.
    if (!IsSystemExplorer(GetCurrentProcess())) return CLASS_E_CLASSNOTAVAILABLE;
    return winrt::make_self<Factory>().as<IClassFactory>()->QueryInterface(riid, ppv);
} catch (...) {
    return winrt::to_hresult();
}

__control_entrypoint(DllExport) STDAPI DllCanUnloadNow() { return S_FALSE; }
