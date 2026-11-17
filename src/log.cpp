#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace {
std::mutex g_mutex;
std::wstring g_path;
}

void LogInit(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 512 * 1024) {
        std::wstring old = path + L".1";
        MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    }
}

void Log(const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_path.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_path.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d.%03d ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
                 t.wSecond, t.wMilliseconds);
        wchar_t wmsg[1024];
        MultiByteToWideChar(CP_UTF8, 0, msg, -1, wmsg, 1024);
        fwprintf(f, L"%s\n", wmsg);
        fclose(f);
    }
}
