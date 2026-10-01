// Barely — a transparent Windows 11 taskbar with no background process.
// Created by AayuShen. Copyright (c) 2026 AayuShen. All rights reserved.
//
// barely_tap.dll — loaded into explorer.exe through the XAML diagnostics API.
//
// It watches the taskbar's XAML tree, finds the Rectangles that paint the taskbar
// background (Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill/#BackgroundStroke)
// and sets their Opacity. A property-changed callback re-applies it if Windows resets it
// (theme change etc.), and a registry watcher picks up new values from barely.exe.
// No polling: when idle, the only thing here is one thread blocked on a registry event.
//
// Security: the DLL refuses to activate in any process other than %SystemRoot%\explorer.exe,
// reads a single clamped DWORD as its only input, and never lets an exception reach explorer.

#include "barely_shared.h"

#include <unknwn.h>
#include <ocidl.h>
#include <xamlom.h>
#undef GetCurrentTime  // winbase.h macro clashes with C++/WinRT

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Xaml.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace wux = winrt::Windows::UI::Xaml;
using winrt::Windows::UI::Core::CoreDispatcher;
using winrt::Windows::UI::Core::CoreDispatcherPriority;

namespace {

struct Target {
    winrt::weak_ref<wux::UIElement> element;
    CoreDispatcher dispatcher{nullptr};
    int64_t token = 0;  // property-changed callback; touched only on the UI thread
};

// Opacity in [0, 1], or < 0 meaning "restore the stock look".
std::atomic<double> g_opacity{-1.0};

winrt::com_ptr<IXamlDiagnostics> g_diag;
std::once_flag g_started;

std::mutex g_lock;  // guards everything below
std::unordered_map<InstanceHandle, InstanceHandle> g_parent;
std::unordered_set<InstanceHandle> g_backgrounds;
std::vector<std::shared_ptr<Target>> g_targets;

double ReadOpacity() {
    DWORD value = 0, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, kRegOpacity, RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS)
        return -1.0;
    return (std::min<DWORD>)(value, 100) / 100.0;
}

// Must run on the element's UI thread.
void Apply(wux::UIElement const& el, Target& t) {
    auto const prop = wux::UIElement::OpacityProperty();
    double const op = g_opacity.load();
    if (op < 0) {
        if (t.token) {
            el.UnregisterPropertyChangedCallback(prop, t.token);
            t.token = 0;
        }
        el.ClearValue(prop);
        return;
    }
    if (!t.token) {
        t.token = el.RegisterPropertyChangedCallback(
            prop, [](wux::DependencyObject const& sender, wux::DependencyProperty const&) {
                double const want = g_opacity.load();
                if (want < 0) return;
                auto e = sender.as<wux::UIElement>();
                if (e.Opacity() != want) e.Opacity(want);
            });
    }
    el.Opacity(op);
}

void ReapplyAll() {
    std::lock_guard lock(g_lock);
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

        if (type == L"Taskbar.TaskbarBackground") {
            g_backgrounds.insert(element.Handle);
            return S_OK;
        }
        if (type != L"Windows.UI.Xaml.Shapes.Rectangle" ||
            (name != L"BackgroundFill" && name != L"BackgroundStroke") ||
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
        g_targets.push_back(t);
        Apply(el, *t);
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

DWORD WINAPI RegistryWatchThread(LPVOID) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_NOTIFY | KEY_QUERY_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return 1;
    HANDLE changed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!changed) return 1;
    while (RegNotifyChangeKeyValue(key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, changed, TRUE) ==
           ERROR_SUCCESS) {
        WaitForSingleObject(changed, INFINITE);
        g_opacity = ReadOpacity();
        ReapplyAll();
    }
    return 0;
}

struct Tap : winrt::implements<Tap, IObjectWithSite, winrt::non_agile> {
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override try {
        m_site.copy_from(site);
        if (!site) return S_OK;
        std::call_once(g_started, [this] {
            g_diag = m_site.as<IXamlDiagnostics>();
            g_opacity = ReadOpacity();
            g_watcher = winrt::make_self<Watcher>();
            if (HANDLE h = CreateThread(nullptr, 0, AdviseThread, nullptr, 0, nullptr))
                CloseHandle(h);
            if (HANDLE h = CreateThread(nullptr, 0, RegistryWatchThread, nullptr, 0, nullptr))
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

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* ppv) try {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (clsid != CLSID_BarelyTap) return CLASS_E_CLASSNOTAVAILABLE;
    // Only ever act on the real Windows shell, never in an arbitrary host process.
    if (!IsSystemExplorer(GetCurrentProcess())) return CLASS_E_CLASSNOTAVAILABLE;
    return winrt::make_self<Factory>().as<IClassFactory>()->QueryInterface(riid, ppv);
} catch (...) {
    return winrt::to_hresult();
}

STDAPI DllCanUnloadNow() { return S_FALSE; }
