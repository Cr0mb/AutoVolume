#pragma once
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace platform {

// ---- single instance: the first copy owns a mutex and listens on two named events
bool AcquireSingleInstance();
enum class Signal { Show, Quit };
bool SignalRunningInstance(Signal s);
HANDLE CreateSignalEvent(Signal s);

// ---- shortcuts
std::wstring StartupShortcutPath();
std::wstring DesktopShortcutPath();
bool CreateShortcut(const std::wstring& lnkPath, const std::wstring& args);
bool IsStartupEnabled();
bool SetStartupEnabled(bool on);

// ---- window chrome
void StyleTitleBar(HWND hwnd, COLORREF caption);

// ---- notification-area icon
class TrayIcon {
public:
    bool Create(HWND owner, UINT callbackMsg, HICON icon, const std::wstring& tip);
    void Recreate();  // after Explorer restarts
    void SetIcon(HICON icon);
    void SetTip(const std::wstring& tip);
    void Notify(const std::wstring& title, const std::wstring& text);
    void Remove();

private:
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
};

}  // namespace platform
