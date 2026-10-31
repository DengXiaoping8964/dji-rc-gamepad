#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace dji {
namespace {

Level g_level = Level::kInfo;
FILE* g_file = nullptr;
CRITICAL_SECTION g_lock;
struct LockInit {
    LockInit() { InitializeCriticalSection(&g_lock); }
} g_lock_init;

const char* level_tag(Level level) {
    switch (level) {
        case Level::kError: return "E";
        case Level::kWarn: return "W";
        case Level::kInfo: return "I";
        case Level::kDebug: return "D";
        default: return "T";
    }
}

}  // namespace

void log_set_level(Level level) { g_level = level; }
Level log_level() { return g_level; }

bool log_open_file(const char* utf8_path) {
    EnterCriticalSection(&g_lock);
    if (g_file != nullptr) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    if (utf8_path != nullptr) {
        wchar_t wide[1024];
        if (MultiByteToWideChar(CP_UTF8, 0, utf8_path, -1, wide, 1024) > 0) {
            g_file = _wfopen(wide, L"ab");
        }
    }
    LeaveCriticalSection(&g_lock);
    return utf8_path == nullptr || g_file != nullptr;
}

void log_close_file() {
    EnterCriticalSection(&g_lock);
    if (g_file != nullptr) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    LeaveCriticalSection(&g_lock);
}

void log_write(Level level, const char* fmt, ...) {
    if (static_cast<int>(level) > static_cast<int>(g_level)) {
        return;
    }
    char body[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    SYSTEMTIME now;
    GetLocalTime(&now);
    char line[2304];
    std::snprintf(line, sizeof(line), "[%02d:%02d:%02d.%03d][%s] %s\n", now.wHour, now.wMinute,
                  now.wSecond, now.wMilliseconds, level_tag(level), body);

    EnterCriticalSection(&g_lock);
    std::fputs(line, stderr);
    std::fflush(stderr);
    if (g_file != nullptr) {
        std::fputs(line, g_file);
        std::fflush(g_file);
    }
    LeaveCriticalSection(&g_lock);
}

}  // namespace dji
