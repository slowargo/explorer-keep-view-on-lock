// ==WindhawkMod==
// @id              explorer-keep-view-on-lock
// @name            Explorer Keep View On Lock
// @description     Stops Explorer file lists from losing scroll position and selection after Win+L lock/unlock
// @version         1.0.0
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
`ImmersiveColorSet` notifications when a snapshot of the relevant color
settings (light/dark, accent, transparency, high contrast) is unchanged.
Real theme/accent changes still pass through.
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
#include <mutex>
#include <set>
#include <string>

namespace {

struct ColorSnapshot {
    DWORD appsLight = 0xFFFFFFFF;
    DWORD systemLight = 0xFFFFFFFF;
    DWORD transparency = 0xFFFFFFFF;
    DWORD colorPrevalence = 0xFFFFFFFF;
    DWORD accentColor = 0xFFFFFFFF;
    DWORD colorizationColor = 0xFFFFFFFF;
    DWORD accentColorMenu = 0xFFFFFFFF;
    DWORD highContrast = 0;

    bool operator==(const ColorSnapshot& o) const {
        return appsLight == o.appsLight && systemLight == o.systemLight &&
               transparency == o.transparency &&
               colorPrevalence == o.colorPrevalence &&
               accentColor == o.accentColor &&
               colorizationColor == o.colorizationColor &&
               accentColorMenu == o.accentColorMenu &&
               highContrast == o.highContrast;
    }
};

std::mutex g_mutex;
ColorSnapshot g_snapshot;
bool g_haveSnapshot = false;
std::set<HWND> g_subclassed;
bool g_verbose = false;

DWORD ReadDword(LPCWSTR subKey, LPCWSTR value) {
    DWORD data = 0xFFFFFFFF;
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, value, RRF_RT_REG_DWORD,
                     nullptr, &data, &size) != ERROR_SUCCESS) {
        return 0xFFFFFFFF;
    }
    return data;
}

ColorSnapshot TakeSnapshot() {
    constexpr LPCWSTR kPersonalize =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    constexpr LPCWSTR kDwm = L"Software\\Microsoft\\Windows\\DWM";
    constexpr LPCWSTR kAccent =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent";

    ColorSnapshot s;
    s.appsLight = ReadDword(kPersonalize, L"AppsUseLightTheme");
    s.systemLight = ReadDword(kPersonalize, L"SystemUsesLightTheme");
    s.transparency = ReadDword(kPersonalize, L"EnableTransparency");
    s.colorPrevalence = ReadDword(kPersonalize, L"ColorPrevalence");
    s.accentColor = ReadDword(kDwm, L"AccentColor");
    s.colorizationColor = ReadDword(kDwm, L"ColorizationColor");
    s.accentColorMenu = ReadDword(kAccent, L"AccentColorMenu");

    HIGHCONTRASTW hc = {sizeof(hc)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
        s.highContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) ? 1 : 0;
    }
    return s;
}

// ImmersiveColorSet is always sent with wParam == 0 and lParam pointing to the
// string; for wParam == 0 the lParam of WM_SETTINGCHANGE is a section name.
bool IsImmersiveColorSet(WPARAM wParam, LPARAM lParam) {
    if (wParam != 0 || !lParam) {
        return false;
    }
    return lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam),
                     L"ImmersiveColorSet") == 0;
}

// Returns true if the notification should be blocked.
bool ShouldBlock() {
    ColorSnapshot now = TakeSnapshot();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_haveSnapshot && now == g_snapshot) {
        return true;
    }
    g_snapshot = now;
    g_haveSnapshot = true;
    return false;
}

LRESULT CALLBACK CabinetSubclassProc(HWND hWnd,
                                     UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     DWORD_PTR dwRefData) {
    if (uMsg == WM_SETTINGCHANGE && IsImmersiveColorSet(wParam, lParam)) {
        bool block = ShouldBlock();
        if (g_verbose) {
            Wh_Log(L"ImmersiveColorSet to %p: %s", hWnd,
                   block ? L"blocked (no color change)" : L"passed");
        }
        if (block) {
            return 0;
        }
    } else if (uMsg == WM_NCDESTROY) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(hWnd,
                                                         CabinetSubclassProc);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_subclassed.erase(hWnd);
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
        if (g_subclassed.count(hWnd)) {
            return;
        }
    }
    if (WindhawkUtils::SetWindowSubclassFromAnyThread(hWnd,
                                                      CabinetSubclassProc, 0)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_subclassed.insert(hWnd);
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
    if (hWnd && !hWndParent && IsCabinetWindow(hWnd)) {
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
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_snapshot = TakeSnapshot();
        g_haveSnapshot = true;
    }
    Wh_SetFunctionHook((void*)CreateWindowExW, (void*)CreateWindowExW_Hook,
                       (void**)&CreateWindowExW_Original);
    return TRUE;
}

void Wh_ModAfterInit() {
    EnumWindows(EnumInitProc, 0);
}

void Wh_ModUninit() {
    std::set<HWND> windows;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        windows.swap(g_subclassed);
    }
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
