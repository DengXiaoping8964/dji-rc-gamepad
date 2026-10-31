#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>

#include "Util.h"

namespace dji {
namespace {

std::string strip_inline_comment(std::string value) {
    const size_t semi = value.find(';');
    const size_t hash = value.find('#');
    size_t cut = std::string::npos;
    if (semi != std::string::npos) {
        cut = semi;
    }
    if (hash != std::string::npos && (cut == std::string::npos || hash < cut)) {
        cut = hash;
    }
    if (cut != std::string::npos) {
        value.erase(cut);
    }
    return trim(value);
}

bool truthy(const std::string& value) {
    const std::string low = to_lower(trim(value));
    return low == "on" || low == "true" || low == "yes" || low == "1";
}

int axis_index_by_name(const std::string& name) {
    for (int i = 0; i < kAnaCount; ++i) {
        if (name == kAnalogNames[i]) {
            return i;
        }
    }
    return -1;
}

std::string slot_text(const IniFile& ini, const std::string& section, const char* key,
                      const std::string& fallback) {
    for (const auto& entry : ini.entries(section)) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    return fallback;
}

long slot_number(const IniFile& ini, const std::string& section, const char* key, long fallback) {
    const std::string text = slot_text(ini, section, key, "");
    long parsed = 0;
    if (!text.empty() && parse_int(text, parsed)) {
        return parsed;
    }
    return fallback;
}

double slot_real(const IniFile& ini, const std::string& section, const char* key, double fallback) {
    const std::string text = slot_text(ini, section, key, "");
    double parsed = 0.0;
    if (!text.empty() && parse_double(text, parsed)) {
        return parsed;
    }
    return fallback;
}

std::string canonical_slot(const std::string& raw) {
    std::string low = to_lower(trim(raw));
    if (low == "dpad_up" || low == "dup" || low == "up") return "DPAD_UP";
    if (low == "dpad_down" || low == "ddown" || low == "down") return "DPAD_DOWN";
    if (low == "dpad_left" || low == "dleft" || low == "left") return "DPAD_LEFT";
    if (low == "dpad_right" || low == "dright" || low == "right") return "DPAD_RIGHT";
    if (low == "start" || low == "menu" || low == "pause") return "START";
    if (low == "back" || low == "select" || low == "view") return "BACK";
    if (low == "lthumb" || low == "ls_click" || low == "l3") return "LS";
    if (low == "rthumb" || low == "rs_click" || low == "r3") return "RS";
    if (low == "l1") return "LB";
    if (low == "r1") return "RB";
    if (low == "l2") return "LT";
    if (low == "r2") return "RT";
    if (low == "guide" || low == "xbox") return "GUIDE";
    for (char& c : low) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return low;
}

}  // namespace

const Binding* Profile::find_analog(const std::string& slot) const {
    for (const auto& binding : analog) {
        if (binding.slot == slot) {
            return &binding;
        }
    }
    return nullptr;
}

bool IniFile::parse(const std::string& text, std::string& error) {
    sections_.clear();
    order_.clear();
    std::string section = "global";
    size_t line_no = 0;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) {
            eol = text.size();
        }
        ++line_no;
        std::string line = trim(text.substr(pos, eol - pos));
        pos = eol + 1;
        if (eol == text.size() && line.empty()) {
            break;
        }
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[') {
            const size_t close = line.find(']');
            if (close == std::string::npos) {
                error = "第 " + std::to_string(line_no) + " 行: 段名缺少 ]";
                return false;
            }
            section = to_lower(trim(line.substr(1, close - 1)));
            if (order_.end() == std::find(order_.begin(), order_.end(), section)) {
                order_.push_back(section);
            }
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            error = "第 " + std::to_string(line_no) + " 行: 缺少 = 号";
            return false;
        }
        const std::string key = to_lower(trim(line.substr(0, equals)));
        const std::string value = strip_inline_comment(line.substr(equals + 1));
        if (key.empty()) {
            error = "第 " + std::to_string(line_no) + " 行: 键名为空";
            return false;
        }
        if (order_.end() == std::find(order_.begin(), order_.end(), section)) {
            order_.push_back(section);
        }
        sections_[section].emplace_back(key, value);
    }
    return true;
}

const std::vector<std::pair<std::string, std::string>>& IniFile::entries(
    const std::string& section) const {
    static const std::vector<std::pair<std::string, std::string>> empty;
    const auto it = sections_.find(to_lower(section));
    return it == sections_.end() ? empty : it->second;
}

bool IniFile::has_section(const std::string& section) const {
    return sections_.find(to_lower(section)) != sections_.end();
}

std::string IniFile::section_name(size_t index) const {
    return index < order_.size() ? order_[index] : std::string{};
}

bool load_global_config(const std::string& path, GlobalConfig& out, std::string& error) {
    std::string text;
    if (!read_text_file(path, text)) {
        error = "读不到配置文件: " + path;
        return false;
    }
    IniFile ini;
    if (!ini.parse(text, error)) {
        return false;
    }

    const std::string port = slot_text(ini, "global", "port", "auto");
    out.port = (port == "auto" || port.empty()) ? std::string{} : port;
    out.schedule.channel_hz =
        static_cast<int>(slot_number(ini, "global", "channel_hz", out.schedule.channel_hz));
    out.schedule.button_hz =
        static_cast<int>(slot_number(ini, "global", "button_hz", out.schedule.button_hz));
    out.schedule.status_hz =
        static_cast<int>(slot_number(ini, "global", "status_hz", out.schedule.status_hz));
    out.schedule.response_timeout_ms = static_cast<int>(
        slot_number(ini, "global", "response_timeout_ms", out.schedule.response_timeout_ms));
    out.watchdog_ms = static_cast<int>(slot_number(ini, "global", "watchdog_ms", out.watchdog_ms));
    out.verbosity = static_cast<int>(slot_number(ini, "global", "verbosity", out.verbosity));
    out.profile = slot_text(ini, "global", "profile", out.profile);
    out.auto_profile = truthy(slot_text(ini, "global", "auto_profile", "on"));
    out.log_file = slot_text(ini, "global", "log", "");

    out.dial.mode = slot_text(ini, "dial", "mode", out.dial.mode);
    out.dial.gain = slot_real(ini, "dial", "gain", out.dial.gain);
    out.dial.decay_ms = static_cast<int>(slot_number(ini, "dial", "decay_ms", out.dial.decay_ms));
    out.dial.release_per_sec =
        slot_real(ini, "dial", "release_per_sec", out.dial.release_per_sec);

    for (int i = 0; i < kAnaCount; ++i) {
        const std::string section = std::string("axis.") + kAnalogNames[i];
        if (!ini.has_section(section)) {
            continue;
        }
        AxisCal& cal = out.axes[i];
        cal.minimum = static_cast<int>(slot_number(ini, section, "min", cal.minimum));
        cal.center = static_cast<int>(slot_number(ini, section, "center", cal.center));
        cal.maximum = static_cast<int>(slot_number(ini, section, "max", cal.maximum));
        cal.deadzone = slot_real(ini, section, "deadzone", cal.deadzone);
        cal.expo = slot_real(ini, section, "expo", cal.expo);
        cal.invert = truthy(slot_text(ini, section, "invert", "off"));
    }
    return true;
}

bool save_global_config(const std::string& path, const GlobalConfig& cfg, std::string& error) {
    char buffer[4096];
    std::string text;
    std::snprintf(buffer, sizeof(buffer),
                  "# dji-rc-bridge 全局配置\n"
                  "# 遥控器 -> 虚拟 Xbox360 手柄。所有值可在 profiles/ 下按游戏覆盖映射。\n"
                  "[global]\n"
                  "port = %s\n"
                  "channel_hz = %d\n"
                  "button_hz = %d\n"
                  "status_hz = %d\n"
                  "response_timeout_ms = %d\n"
                  "watchdog_ms = %d\n"
                  "profile = %s\n"
                  "auto_profile = %s\n"
                  "verbosity = %d\n"
                  "log = %s\n\n"
                  "[dial]\n"
                  "mode = %s\n"
                  "gain = %.2f\n"
                  "decay_ms = %d\n"
                  "release_per_sec = %.2f\n\n",
                  cfg.port.empty() ? "auto" : cfg.port.c_str(), cfg.schedule.channel_hz,
                  cfg.schedule.button_hz, cfg.schedule.status_hz, cfg.schedule.response_timeout_ms,
                  cfg.watchdog_ms, cfg.profile.c_str(), cfg.auto_profile ? "on" : "off",
                  cfg.verbosity, cfg.log_file.c_str(), cfg.dial.mode.c_str(), cfg.dial.gain,
                  cfg.dial.decay_ms, cfg.dial.release_per_sec);
    text = buffer;
    for (int i = 0; i < kAnaCount; ++i) {
        const AxisCal& cal = cfg.axes[i];
        std::snprintf(buffer, sizeof(buffer),
                      "[axis.%s]\nmin = %d\ncenter = %d\nmax = %d\ndeadzone = %.3f\n"
                      "expo = %.3f\ninvert = %s\n\n",
                      kAnalogNames[i], cal.minimum, cal.center, cal.maximum, cal.deadzone,
                      cal.expo, cal.invert ? "on" : "off");
        text += buffer;
    }
    if (!write_text_file(path, text)) {
        error = "写入失败: " + path;
        return false;
    }
    return true;
}

bool parse_profile_text(const std::string& text, Profile& out, std::string& error) {
    IniFile ini;
    if (!ini.parse(text, error)) {
        return false;
    }

    out = Profile{};
    out.name = slot_text(ini, "meta", "name", "default");
    if (out.name.empty()) {
        out.name = "default";
    }
    out.match_process = to_lower(slot_text(ini, "auto", "process", ""));
    out.match_title = to_lower(slot_text(ini, "auto", "title", ""));
    out.hold_ms = static_cast<int>(slot_number(ini, "hold", "ms", out.hold_ms));

    for (const auto& entry : ini.entries("modifiers")) {
        out.modifiers.emplace_back(to_lower(entry.first), entry.second);
    }
    for (const auto& entry : ini.entries("pad")) {
        Binding binding;
        const size_t dot = entry.first.find('.');
        binding.slot = canonical_slot(entry.first.substr(0, dot));
        if (dot != std::string::npos) {
            binding.modifier = to_lower(entry.first.substr(dot + 1));
        }
        binding.source = to_lower(entry.second);
        if (!binding.source.empty() && binding.source != "none") {
            out.digital.push_back(binding);
        }
    }
    for (const auto& entry : ini.entries("axis")) {
        Binding binding;
        binding.slot = canonical_slot(entry.first);
        binding.source = to_lower(entry.second);
        if (!binding.source.empty() && binding.source != "none") {
            out.analog.push_back(binding);
        }
    }
    return true;
}

bool load_profile(const std::string& path, Profile& out, std::string& error) {
    std::string text;
    if (!read_text_file(path, text)) {
        error = "读不到映射文件: " + path;
        return false;
    }
    return parse_profile_text(text, out, error);
}

std::vector<std::string> list_profiles(const std::string& directory) {
    std::vector<std::string> names;
    const std::wstring pattern = utf8_to_wide(directory) + L"\\*.ini";
    WIN32_FIND_DATAW found{};
    const HANDLE handle = FindFirstFileW(pattern.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        return names;
    }
    do {
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            std::string name = wide_to_utf8(found.cFileName);
            const size_t dot = name.find_last_of('.');
            if (dot != std::string::npos) {
                name.erase(dot);
            }
            names.push_back(name);
        }
    } while (FindNextFileW(handle, &found));
    FindClose(handle);
    std::sort(names.begin(), names.end());
    return names;
}

}  // namespace dji
