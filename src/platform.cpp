#include "platform.h"

#include <dwmapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "log.h"
#include "util.h"

using Microsoft::WRL::ComPtr;

namespace platform {

namespace {
constexpr wchar_t kMutexName[] = L"Local\\AutoVolumeSingleInstance";
constexpr wchar_t kShowEvent[] = L"Local\\AutoVolume.Show";
constexpr wchar_t kQuitEvent[] = L"Local\\AutoVolume.Quit";
HANDLE g_mutex = nullptr;

std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}
}  // namespace

bool AcquireSingleInstance() {
    g_mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    return g_mutex && GetLastError() != ERROR_ALREADY_EXISTS;
}

bool SignalRunningInstance(Signal s) {
    HANDLE h = OpenEventW(EVENT_MODIFY_STATE, FALSE, s == Signal::Show ? kShowEvent : kQuitEvent);
    if (!h) return false;
    SetEvent(h);
    CloseHandle(h);
    return true;
}

HANDLE CreateSignalEvent(Signal s) {
    return CreateEventW(nullptr, FALSE, FALSE, s == Signal::Show ? kShowEvent : kQuitEvent);
}

std::wstring StartupShortcutPath() { return KnownFolder(FOLDERID_Startup) + L"\\AutoVolume.lnk"; }
std::wstring DesktopShortcutPath() { return KnownFolder(FOLDERID_Desktop) + L"\\AutoVolume.lnk"; }

bool CreateShortcut(const std::wstring& lnkPath, const std::wstring& args) {
    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) return false;
    const std::wstring exe = ExePath();
    link->SetPath(exe.c_str());
    link->SetArguments(args.c_str());
    link->SetWorkingDirectory(ExeDir().c_str());
    link->SetIconLocation(exe.c_str(), 0);
    link->SetDescription(L"Automatic volume leveler for TV and movies");
    ComPtr<IPersistFile> file;
    if (FAILED(link.As(&file))) return false;
    return SUCCEEDED(file->Save(lnkPath.c_str(), TRUE));
}

bool IsStartupEnabled() { return GetFileAttributesW(StartupShortcutPath().c_str()) != INVALID_FILE_ATTRIBUTES; }

bool SetStartupEnabled(bool on) {
    if (on) return CreateShortcut(StartupShortcutPath(), L"--minimized");
    DeleteFileW(StartupShortcutPath().c_str());
    return !IsStartupEnabled();
}

void StyleTitleBar(HWND hwnd, COLORREF caption) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR (Win11)*/, &caption, sizeof(caption));
}

// ---------------------------------------------------------------- tray
bool TrayIcon::Create(HWND owner, UINT callbackMsg, HICON icon, const std::wstring& tip) {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = owner;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = callbackMsg;
    nid_.hIcon = icon;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    return added_;
}

void TrayIcon::Recreate() {
    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void TrayIcon::SetIcon(HICON icon) {
    nid_.hIcon = icon;
    nid_.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void TrayIcon::SetTip(const std::wstring& tip) {
    if (tip == nid_.szTip) return;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
}

void TrayIcon::Notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = nid_;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    n.hBalloonIcon = nid_.hIcon;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

void TrayIcon::Remove() {
    if (added_) Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
}

}  // namespace platform
