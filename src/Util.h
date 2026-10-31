#pragma once
#include <cstdint>
#include <string>

namespace dji {

double now_seconds();
void sleep_ms(uint32_t ms);

std::string trim(const std::string& text);
std::string to_lower(std::string text);
bool parse_int(const std::string& text, long& out);
bool parse_double(const std::string& text, double& out);

std::string wide_to_utf8(const wchar_t* wide);
std::wstring utf8_to_wide(const std::string& text);

bool read_text_file(const std::string& path, std::string& out);
bool write_text_file(const std::string& path, const std::string& content);
// Directory holding the executable, UTF-8, without a trailing separator.
std::string exe_directory();

// Foreground window: process leaf name (e.g. "GTA5.exe") and title, both UTF-8.
bool foreground_window(std::string& process_name, std::string& title);

}  // namespace dji
