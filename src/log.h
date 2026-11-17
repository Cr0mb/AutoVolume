#pragma once
#include <string>

// Tiny thread-safe file logger (autovolume.log next to the exe, rotated at 512 KB).
void LogInit(const std::wstring& path);
void Log(const char* fmt, ...);
