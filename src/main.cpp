// AutoVolume - keeps TV shows and movies at a steady loudness by riding the Windows master volume.
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d3d11.h>

#include <string>

#include "engine.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "log.h"
#include "platform.h"
#include "resource.h"
#include "settings.h"
#include "ui.h"
#include "util.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr UINT WM_TRAY = WM_APP + 1;
enum : UINT { ID_OPEN = 100, ID_TOGGLE, ID_QUIT };
constexpr float kBaseW = 1000.0f, kBaseH = 860.0f;

struct AppState {
    HINSTANCE inst = nullptr;
    HWND hwnd = nullptr;
    Settings* settings = nullptr;
    Engine* engine = nullptr;
    platform::TrayIcon tray;
    HICON iconOn = nullptr, iconOff = nullptr;
    UINT taskbarCreated = 0;
    bool quit = false;
    bool hideNoticeShown = false;
    UINT resizeW = 0, resizeH = 0;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
} g;

// ---------------------------------------------------------------- Direct3D 11
void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g.swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g.device->CreateRenderTargetView(back, nullptr, &g.rtv);
        back->Release();
    }
}

void CleanupRenderTarget() {
    if (g.rtv) g.rtv->Release(), g.rtv = nullptr;
}

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate = {60, 1};
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &g.swapChain, &g.device, &got, &g.context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                           &g.swapChain, &g.device, &got, &g.context);
    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g.swapChain) g.swapChain->Release(), g.swapChain = nullptr;
    if (g.context) g.context->Release(), g.context = nullptr;
    if (g.device) g.device->Release(), g.device = nullptr;
}

// ---------------------------------------------------------------- window helpers
void ShowMain() {
    ShowWindow(g.hwnd, IsIconic(g.hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(g.hwnd);
}

void HideMain() {
    ShowWindow(g.hwnd, SW_HIDE);
    if (!g.hideNoticeShown) {
        g.hideNoticeShown = true;
        g.tray.Notify(L"AutoVolume", L"Still running in the system tray. Right-click the icon to quit.");
    }
}

void ShowTrayMenu(int x, int y) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_OPEN, L"Open AutoVolume");
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    AppendMenuW(m, MF_STRING | (g.settings->Get().enabled ? MF_CHECKED : 0), ID_TOGGLE, L"Leveling enabled");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_QUIT, L"Quit");
    SetForegroundWindow(g.hwnd);
    UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TrackPopupMenuEx(m, TPM_RIGHTBUTTON | align | TPM_BOTTOMALIGN, x, y, g.hwnd, nullptr);
    PostMessageW(g.hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

// Dark context menus (uxtheme ordinals used by Explorer/Notepad++; silently skipped if unavailable).
void EnableDarkMenus() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetPreferredAppMode = int(WINAPI*)(int);
    using FlushMenuThemes = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppMode>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemes>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(2 /*ForceDark*/);
    if (flush) flush();
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) g.resizeW = LOWORD(lParam), g.resizeH = HIWORD(lParam);
        return 0;
    case WM_CLOSE:
        HideMain();
        return 0;
    case WM_TRAY:
        switch (LOWORD(lParam)) {
        case NIN_SELECT:
        case NIN_KEYSELECT:
        case WM_LBUTTONDBLCLK:
            ShowMain();
            break;
        case WM_CONTEXTMENU:
            ShowTrayMenu(GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam));
            break;
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_OPEN: ShowMain(); break;
        case ID_TOGGLE: g.settings->Update([](Core& c) { c.enabled = !c.enabled; }); break;
        case ID_QUIT: g.quit = true; break;
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;  // no ALT menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        if (msg == g.taskbarCreated && g.taskbarCreated) {
            g.tray.Recreate();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::wstring TrayTip(const EngineStatus& st, bool enabled) {
    std::wstring tip = L"AutoVolume";
    if (st.hasVol) tip += L" - volume " + std::to_wstring((int)std::lround(st.volPct)) + L"%";
    if (!enabled) tip += L" (paused)";
    return tip;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    bool minimized = false, quitOther = false, installShortcut = false;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i < argc; ++i) {
            std::wstring a = argv[i];
            if (a == L"--minimized") minimized = true;
            else if (a == L"--quit") quitOther = true;
            else if (a == L"--install-shortcut") installShortcut = true;
        }
        LocalFree(argv);
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (installShortcut) {
        bool ok = platform::CreateShortcut(platform::DesktopShortcutPath(), L"");
        CoUninitialize();
        return ok ? 0 : 1;
    }

    const std::wstring dir = ExeDir();
    LogInit(dir + L"autovolume.log");
    if (!platform::AcquireSingleInstance()) {  // hand off to the copy that's already running
        if (quitOther) platform::SignalRunningInstance(platform::Signal::Quit);
        else if (!minimized) platform::SignalRunningInstance(platform::Signal::Show);
        return 0;
    }
    if (quitOther) return 0;
    HANDLE showEvent = platform::CreateSignalEvent(platform::Signal::Show);
    HANDLE quitEvent = platform::CreateSignalEvent(platform::Signal::Quit);

    Settings settings(dir + L"settings.json");
    settings.Load();
    Engine engine(settings);
    engine.Start();
    g.inst = inst;
    g.settings = &settings;
    g.engine = &engine;
    Log("started");

    // ---- window, sized for the monitor's DPI and work area
    EnableDarkMenus();
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    const float dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
    const float workW = (float)(mi.rcWork.right - mi.rcWork.left), workH = (float)(mi.rcWork.bottom - mi.rcWork.top);
    const float scale = std::min({dpiScale, (workH - 48.0f * dpiScale) / kBaseH, (workW - 16.0f) / kBaseW});

    g.iconOn = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                 GetSystemMetrics(SM_CYSMICON), 0);
    g.iconOff = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP_OFF), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                  GetSystemMetrics(SM_CYSMICON), 0);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                 GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = g.iconOn;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB((ui::kBackground >> 16) & 255, (ui::kBackground >> 8) & 255, ui::kBackground & 255));
    wc.lpszClassName = L"AutoVolumeWindow";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, (LONG)(kBaseW * scale), (LONG)(kBaseH * scale)};
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    const int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    const int wx = mi.rcWork.left + (int)(workW - ww) / 2, wy = mi.rcWork.top + std::max(0, (int)(workH - wh) / 2);
    g.hwnd = CreateWindowExW(0, wc.lpszClassName, L"AutoVolume", style, wx, wy, ww, wh, nullptr, nullptr, inst, nullptr);
    platform::StyleTitleBar(g.hwnd, RGB((ui::kBackground >> 16) & 255, (ui::kBackground >> 8) & 255, ui::kBackground & 255));
    if (!CreateDeviceD3D(g.hwnd)) {
        Log("Direct3D 11 init failed");
        MessageBoxW(nullptr, L"Could not initialise Direct3D 11.", L"AutoVolume", MB_ICONERROR);
        CleanupDeviceD3D();
        engine.Stop();
        return 1;
    }

    g.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    g.tray.Create(g.hwnd, WM_TRAY, settings.Get().enabled ? g.iconOn : g.iconOff, L"AutoVolume");
    bool trayEnabledShown = settings.Get().enabled;

    // ---- Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ui::SetupStyle(scale);

    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    auto addFont = [&](const wchar_t* file) -> ImFont* {
        std::wstring p = std::wstring(winDir) + L"\\Fonts\\" + file;
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        return io.Fonts->AddFontFromFileTTF(ToUtf8(p).c_str(), 15.0f);
    };
    UiContext ctx;
    ctx.settings = &settings;
    ctx.engine = &engine;
    ctx.scale = scale;
    ctx.regular = addFont(L"segoeui.ttf");
    if (!ctx.regular) ctx.regular = io.Fonts->AddFontDefault();
    ctx.semibold = addFont(L"seguisb.ttf");
    if (!ctx.semibold) ctx.semibold = ctx.regular;
    io.FontDefault = ctx.regular;
    ctx.startupEnabled = platform::IsStartupEnabled();

    ImGui_ImplWin32_Init(g.hwnd);
    ImGui_ImplDX11_Init(g.device, g.context);

    if (!minimized) ShowMain();

    // ---- main loop: render ~60 fps while visible, otherwise just wait for messages/signals
    const float clear[4] = {((ui::kBackground >> 16) & 255) / 255.0f, ((ui::kBackground >> 8) & 255) / 255.0f,
                            (ui::kBackground & 255) / 255.0f, 1.0f};
    HANDLE signals[2] = {showEvent, quitEvent};
    double lastFrame = 0, lastHousekeeping = 0;
    while (!g.quit) {
        const bool visible = IsWindowVisible(g.hwnd) && !IsIconic(g.hwnd);
        const double sinceFrame = NowSeconds() - lastFrame;
        const DWORD wait = visible ? (DWORD)std::max(0.0, (1.0 / 60.0 - sinceFrame) * 1000.0) : 250;
        DWORD r = MsgWaitForMultipleObjectsEx(2, signals, wait, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (r == WAIT_OBJECT_0) ShowMain();
        if (r == WAIT_OBJECT_0 + 1) g.quit = true;

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) g.quit = true;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (g.quit) break;

        const double now = NowSeconds();
        if (now - lastHousekeeping > 1.0) {
            lastHousekeeping = now;
            settings.SaveIfDirty();
            const bool en = settings.Get().enabled;
            if (en != trayEnabledShown) {
                g.tray.SetIcon(en ? g.iconOn : g.iconOff);
                trayEnabledShown = en;
            }
            g.tray.SetTip(TrayTip(engine.Status(), en));
        }

        if (!visible || now - lastFrame < 1.0 / 62.0) continue;
        lastFrame = now;
        if (g.resizeW && g.resizeH) {
            CleanupRenderTarget();
            g.swapChain->ResizeBuffers(0, g.resizeW, g.resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g.resizeW = g.resizeH = 0;
            CreateRenderTarget();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        UiActions act;
        ui::Draw(ctx, act);
        ImGui::Render();
        g.context->OMSetRenderTargets(1, &g.rtv, nullptr);
        g.context->ClearRenderTargetView(g.rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g.swapChain->Present(1, 0);

        if (act.hide) HideMain();
        if (act.quit) g.quit = true;
        if (act.toggleStartup) {
            if (!platform::SetStartupEnabled(!ctx.startupEnabled)) Log("could not change startup shortcut");
            ctx.startupEnabled = platform::IsStartupEnabled();
        }
    }

    // ---- shutdown: engine restores your volume, then save and clean up
    engine.Stop();
    settings.Save();
    g.tray.Remove();
    Log("stopped");
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g.hwnd);
    UnregisterClassW(wc.lpszClassName, inst);
    CoUninitialize();
    return 0;
}
