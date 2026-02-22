#pragma once

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

inline void RemixLog(const char* fmt, ...) {
    static FILE* s_file = nullptr;
    static bool s_checkedConfig = false;
    static bool s_enabled = false;

    if (!s_checkedConfig) {
        s_checkedConfig = true;
        char iniPath[MAX_PATH] = {};
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&RemixLog), &hSelf);
        GetModuleFileNameA(hSelf, iniPath, MAX_PATH);
        char* slash = std::strrchr(iniPath, '\\');
        if (slash) {
            std::strcpy(slash + 1, "camera_proxy.ini");
        } else {
            std::strcpy(iniPath, "camera_proxy.ini");
        }
        s_enabled = GetPrivateProfileIntA("CameraProxy", "EnableRemixApiLog", 0, iniPath) != 0;
    }

    if (!s_enabled) return;

    if (!s_file) {
        char path[MAX_PATH] = {};
        HMODULE hSelf = nullptr;
        GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&RemixLog), &hSelf);
        GetModuleFileNameA(hSelf, path, MAX_PATH);
        char* slash = std::strrchr(path, '\\');
        if (slash)
            strcpy_s(slash + 1, MAX_PATH - static_cast<int>(slash - path) - 1, "remix_api.log");
        else
            strcpy_s(path, sizeof(path), "remix_api.log");
        s_file = fopen(path, "a");
    }
    if (!s_file) return;

    time_t now = time(nullptr);
    struct tm t = {};
    localtime_s(&t, &now);
    fprintf(s_file, "[%02d:%02d:%02d] ", t.tm_hour, t.tm_min, t.tm_sec);

    va_list args;
    va_start(args, fmt);
    vfprintf(s_file, fmt, args);
    va_end(args);
    fprintf(s_file, "\n");
    fflush(s_file);
}
