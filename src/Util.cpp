#include "Util.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <vector>

namespace dji {

double now_seconds() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) / static_cast<double>(freq.QuadPart);
}

void sleep_ms(uint32_t ms) { Sleep(ms); }

std::string trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                           text[begin] == '\n')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                           text[end - 1] == '\n')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string to_lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool parse_int(const std::string& text, long& out) {
    const std::string value = trim(text);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 0);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = parsed;
    return true;
}

bool parse_double(const std::string& text, double& out) {
    const std::string value = trim(text);
    if (value.empty()) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    out = parsed;
    return true;
}

std::string wide_to_utf8(const wchar_t* wide) {
    if (wide == nullptr) {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(len) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), len);
    return out;
}

bool read_text_file(const std::string& path, std::string& out) {
    const std::wstring wide = utf8_to_wide(path);
    FILE* f = _wfopen(wide.c_str(), L"rb");
    if (f == nullptr) {
        return false;
    }
    // Strip a UTF-8 BOM if present.
    char buffer[4096];
    std::string raw;
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        raw.append(buffer, got);
    }
    std::fclose(f);
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB && static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(raw.begin(), raw.begin() + 3);
    }
    out = raw;
    return true;
}

bool write_text_file(const std::string& path, const std::string& content) {
    const std::wstring wide = utf8_to_wide(path);
    FILE* f = _wfopen(wide.c_str(), L"wb");
    if (f == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return written == content.size();
}

std::string exe_directory() {
    wchar_t path[32768];
    const DWORD len = GetModuleFileNameW(nullptr, path, 32768);
    if (len == 0 || len >= 32768) {
        return {};
    }
    std::wstring full(path, len);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    full.erase(slash);
    return wide_to_utf8(full.c_str());
}

bool foreground_window(std::string& process_name, std::string& title) {
    const HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) {
        return false;
    }
    wchar_t text[1024];
    const int n = GetWindowTextW(hwnd, text, 1024);
    title = n > 0 ? wide_to_utf8(text) : std::string{};

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    wchar_t image[1024];
    DWORD size = 1024;
    process_name.clear();
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process != nullptr) {
        if (QueryFullProcessImageNameW(process, 0, image, &size)) {
            std::wstring full(image, size);
            const size_t slash = full.find_last_of(L"\\/");
            process_name = wide_to_utf8((slash == std::wstring::npos ? full : full.substr(slash + 1)).c_str());
        }
        CloseHandle(process);
    }
    return true;
}

}  // namespace dji
