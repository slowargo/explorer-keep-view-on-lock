// ==WindhawkMod==
// @id              explorer-keep-view-on-lock
// @name            Explorer Keep View On Lock
// @description     Stops Explorer file lists from losing scroll position and selection after Win+L lock/unlock
// @version         1.1.0
// @author          slowargo
// @github          https://github.com/slowargo
// @include         explorer.exe
// @compilerOptions -lcomctl32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Keep View On Lock

On lock (Win+L) Windows broadcasts `WM_SETTINGCHANGE("ImmersiveColorSet")`
twice even when no color/theme setting changed. Explorer folder windows react
by rebuilding the items view, which resets scroll position and selection.

This mod subclasses Explorer folder windows (`CabinetWClass`) and swallows
`ImmersiveColorSet` notifications when that window's snapshot of the color
settings (light/dark, accent, transparency, accent on title bars, current
theme, high contrast scheme, system colors) is unchanged. Real theme/accent
changes still pass through. If a notification is blocked, the settings are
checked again ~400 ms later and the notification is delivered then if
something did change (covers senders that broadcast before writing settings).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- verboseLog: false
  $name: Verbose log
  $description: Log every ImmersiveColorSet notification and whether it was blocked
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>
#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

namespace {

constexpr UINT_PTR kRecheckTimerId = 0x4B56574C;  // 'KVWL'
constexpr UINT kRecheckDelayMs = 400;
constexpr WCHAR kImmersiveColorSet[] = L"ImmersiveColorSet";

struct ColorSnapshot {
    DWORD appsLight = 0;
    DWORD systemLight = 0;
    DWORD transparency = 0;
    DWORD colorPrevalence = 0;
    DWORD dwmColorPrevalence = 0;
    DWORD accentColor = 0;
    DWORD colorizationColor = 0;
    DWORD accentColorMenu = 0;
    DWORD highContrastFlags = 0;
    ULONGLONG highContrastScheme = 0;
    ULONGLONG currentTheme = 0;
    ULONGLONG sysColors = 0;

    bool operator==(const ColorSnapshot& o) const {
        return appsLight == o.appsLight && systemLight == o.systemLight &&
               transparency == o.transparency &&
               colorPrevalence == o.colorPrevalence &&
               dwmColorPrevalence == o.dwmColorPrevalence &&
               accentColor == o.accentColor &&
               colorizationColor == o.colorizationColor &&
               accentColorMenu == o.accentColorMenu &&
               highContrastFlags == o.highContrastFlags &&
               highContrastScheme == o.highContrastScheme &&
               currentTheme == o.currentTheme && sysColors == o.sysColors;
    }
    bool operator!=(const ColorSnapshot& o) const { return !(*this == o); }
};

std::mutex g_mutex;
std::map<HWND, ColorSnapshot> g_windows;  // subclassed window -> its snapshot
std::atomic<bool> g_verbose{false};

// FNV-1a.
ULONGLONG Hash(const void* data, size_t size, ULONGLONG h = 1469598103934665603ULL) {
    const BYTE* p = static_cast<const BYTE*>(data);
    for (size_t i = 0; i < size; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

ULONGLONG HashString(LPCWSTR s) {
    return s ? Hash(s, lstrlenW(s) * sizeof(WCHAR)) : 0;
}

DWORD ReadDword(LPCWSTR subKey, LPCWSTR value) {
    DWORD data = 0xFFFFFFFF;
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_DWORD,
                     nullptr, &data, &size) != ERROR_SUCCESS) {
        return 0xFFFFFFFF;
    }
    return data;
}

ULONGLONG ReadStringHash(LPCWSTR subKey, LPCWSTR value) {
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_SZ, nullptr,
                     nullptr, &size) != ERROR_SUCCESS ||
        size == 0) {
        return 0;
    }
    std::vector<WCHAR> buf(size / sizeof(WCHAR) + 1);
    size = static_cast<DWORD>(buf.size() * sizeof(WCHAR));
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_SZ, nullptr,
                     buf.data(), &size) != ERROR_SUCCESS) {
        return 0;
    }
    return HashString(buf.data());
}

ColorSnapshot TakeSnapshot() {
    constexpr LPCWSTR kPersonalize =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    constexpr LPCWSTR kThemes =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes";
    constexpr LPCWSTR kDwm = L"Software\\Microsoft\\Windows\\DWM";
    constexpr LPCWSTR kAccent =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";

    ColorSnapshot s;
    s.appsLight = ReadDword(kPersonalize, L"AppsUseLightTheme");
    s.systemLight = ReadDword(kPersonalize, L"SystemUsesLightTheme");
    s.transparency = ReadDword(kPersonalize, L"EnableTransparency");
    s.colorPrevalence = ReadDword(kPersonalize, L"ColorPrevalence");
    s.dwmColorPrevalence = ReadDword(kDwm, L"ColorPrevalence");
    s.accentColor = ReadDword(kDwm, L"AccentColor");
    s.colorizationColor = ReadDword(kDwm, L"ColorizationColor");
    s.accentColorMenu = ReadDword(kAccent, L"AccentColorMenu");
    s.currentTheme = ReadStringHash(kThemes, L"CurrentTheme");

    HIGHCONTRASTW hc = {sizeof(hc)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
        s.highContrastFlags = hc.dwFlags & HCF_HIGHCONTRASTON;
        s.highContrastScheme = HashString(hc.lpszDefaultScheme);
    }

    DWORD colors[31];
    for (int i = 0; i < 31; i++) {
        colors[i] = GetSysColor(i);
    }
    s.sysColors = Hash(colors, sizeof(colors));
    return s;
}

// For wParam == 0 the lParam of WM_SETTINGCHANGE is NULL or a section-name
// string (user32 marshals it as a string across processes).
bool IsImmersiveColorSet(WPARAM wParam, LPARAM lParam) {
    if (wParam != 0 || !lParam) {
        return false;
    }
    return lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam), kImmersiveColorSet) ==
           0;
}

// Compares the current settings with this window's snapshot. Returns true if
// they changed (and updates the snapshot).
bool SettingsChangedFor(HWND hWnd) {
    ColorSnapshot now = TakeSnapshot();
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_windows.find(hWnd);
    if (it == g_windows.end()) {
        g_windows[hWnd] = now;
        return true;
    }
    if (it->second == now) {
        return false;
    }
    it->second = now;
    return true;
}

LRESULT CALLBACK CabinetSubclassProc(HWND hWnd,
                                     UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     DWORD_PTR dwRefData) {
    switch (uMsg) {
        case WM_SETTINGCHANGE:
            if (IsImmersiveColorSet(wParam, lParam)) {
                if (SettingsChangedFor(hWnd)) {
                    if (g_verbose) {
                        Wh_Log(L"ImmersiveColorSet to %p: passed", hWnd);
                    }
                    break;
                }
                if (g_verbose) {
                    Wh_Log(L"ImmersiveColorSet to %p: blocked, recheck in %u ms",
                           hWnd, kRecheckDelayMs);
                }
                // Message-based timer (no TIMERPROC), so nothing calls into
                // the mod if it fires after unload.
                SetTimer(hWnd, kRecheckTimerId, kRecheckDelayMs, nullptr);
                return 0;
            }
            break;

        case WM_TIMER:
            if (wParam == kRecheckTimerId) {
                KillTimer(hWnd, kRecheckTimerId);
                if (SettingsChangedFor(hWnd)) {
                    if (g_verbose) {
                        Wh_Log(L"Recheck %p: settings changed, delivering",
                               hWnd);
                    }
                    DefSubclassProc(hWnd, WM_SETTINGCHANGE, 0,
                                    reinterpret_cast<LPARAM>(kImmersiveColorSet));
                }
                return 0;
            }
            break;

        case WM_NCDESTROY: {
            // The Windhawk subclass wrapper already removed the subclass.
            KillTimer(hWnd, kRecheckTimerId);
            std::lock_guard<std::mutex> lock(g_mutex);
            g_windows.erase(hWnd);
            break;
        }
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

bool IsCabinetWindow(HWND hWnd) {
    wchar_t cls[64];
    return GetClassNameW(hWnd, cls, ARRAYSIZE(cls)) &&
           lstrcmpW(cls, L"CabinetWClass") == 0;
}

void SubclassCabinet(HWND hWnd) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_windows.count(hWnd)) {
            return;
        }
    }
    ColorSnapshot snap = TakeSnapshot();
    if (WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd,
                                                      CabinetSubclassProc, 0)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_windows.emplace(hWnd, snap);
        Wh_Log(L"Subclassed CabinetWClass %p", hWnd);
    }
}

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t CreateWindowExW_Original;

HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle,
                                 LPCWSTR lpClassName,
                                 LPCWSTR lpWindowName,
                                 DWORD dwStyle,
                                 int X,
                                 int Y,
                                 int nWidth,
                                 int nHeight,
                                 HWND hWndParent,
                                 HMENU hMenu,
                                 HINSTANCE hInstance,
                                 LPVOID lpParam) {
    HWND hWnd = CreateWindowExW_Original(dwExStyle, lpClassName, lpWindowName,
                                         dwStyle, X, Y, nWidth, nHeight,
                                         hWndParent, hMenu, hInstance, lpParam);
    // Top-level (possibly owned) windows only; hWndParent is the owner then.
    if (hWnd && !(dwStyle & WS_CHILD) && IsCabinetWindow(hWnd)) {
        SubclassCabinet(hWnd);
    }
    return hWnd;
}

BOOL CALLBACK EnumInitProc(HWND hWnd, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hWnd, &pid);
    if (pid == GetCurrentProcessId() && IsCabinetWindow(hWnd)) {
        SubclassCabinet(hWnd);
    }
    return TRUE;
}

void LoadSettings() {
    g_verbose = Wh_GetIntSetting(L"verboseLog") != 0;
}

}  // namespace

BOOL Wh_ModInit() {
    LoadSettings();
    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook,
                       (void**)&CreateWindowExW_Original);
    return TRUE;
}

void Wh_ModAfterInit() {
    EnumWindows(EnumInitProc, 0);
}

void Wh_ModUninit() {
    std::vector<HWND> windows;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto& kv : g_windows) {
            windows.push_back(kv.first);
        }
        g_windows.clear();
    }
    // A pending recheck timer that fires after this just reaches Explorer's
    // own window proc as an unknown WM_TIMER id and is ignored.
    for (HWND hWnd : windows) {
        if (IsWindow(hWnd)) {
            WindhawkUtils::RemoveWindowSubclassFromAnyThread(
                hWnd, CabinetSubclassProc);
        }
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}
