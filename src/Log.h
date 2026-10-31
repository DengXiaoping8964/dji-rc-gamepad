#pragma once
#include <cstdarg>
#include <cstdint>

namespace dji {

enum class Level : int { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3, kTrace = 4 };

void log_set_level(Level level);
Level log_level();
// Mirrors every line into a file next to the executable; pass nullptr to disable.
bool log_open_file(const char* utf8_path);
void log_close_file();
void log_write(Level level, const char* fmt, ...);

inline void log_set_min_level_from_verbosity(int verbosity) {
    log_set_level(static_cast<Level>(verbosity < 0 ? 0 : (verbosity > 4 ? 4 : verbosity)));
}

}  // namespace dji

#define LOGE(...) ::dji::log_write(::dji::Level::kError, __VA_ARGS__)
#define LOGW(...) ::dji::log_write(::dji::Level::kWarn, __VA_ARGS__)
#define LOGI(...) ::dji::log_write(::dji::Level::kInfo, __VA_ARGS__)
#define LOGD(...) ::dji::log_write(::dji::Level::kDebug, __VA_ARGS__)
#define LOGT(...) ::dji::log_write(::dji::Level::kTrace, __VA_ARGS__)
