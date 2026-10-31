#pragma once
#include <map>
#include <string>
#include <vector>

#include "Link.h"
#include "RcState.h"

namespace dji {

struct DialConfig {
    std::string mode = "accumulate";  // accumulate | absolute
    double gain = 3.5;                // wheel ticks -> trigger travel
    int decay_ms = 300;               // idle time before the trigger releases
    double release_per_sec = 4.0;     // how fast the trigger falls back to zero
};

struct GlobalConfig {
    std::string port;  // empty = auto-detect
    RcLink::Schedule schedule{};
    DialConfig dial{};
    std::string profile = "default";
    bool auto_profile = true;
    int watchdog_ms = 300;  // no channel frame this long -> pad goes neutral
    int verbosity = 2;
    std::string log_file;   // empty = stderr only
    AxisCal axes[kAnaCount];
};

struct Binding {
    std::string slot;      // "A", "DPAD_UP", "LT", ...
    std::string source;    // expression, see Mapping.cpp
    std::string modifier;  // empty = base binding
};

struct Profile {
    std::string name;
    std::string match_process;  // [auto] process = GTA5.exe
    std::string match_title;    // [auto] title = Grand Theft
    int hold_ms = 350;          // press duration that turns a key into a shift layer
    std::vector<std::pair<std::string, std::string>> modifiers;  // "L" -> "btn.fn"
    std::vector<Binding> digital;
    std::vector<Binding> analog;

    const Binding* find_analog(const std::string& slot) const;
};

// Minimal key=value/section reader. Values keep their case; keys are lower-cased.
class IniFile {
public:
    bool parse(const std::string& text, std::string& error);
    // Values of a section, in file order, keys already lower-cased.
    const std::vector<std::pair<std::string, std::string>>& entries(const std::string& section) const;
    bool has_section(const std::string& section) const;
    std::string section_name(size_t index) const;
    size_t section_count() const { return order_.size(); }

private:
    std::map<std::string, std::vector<std::pair<std::string, std::string>>> sections_;
    std::vector<std::string> order_;
};

bool load_global_config(const std::string& path, GlobalConfig& out, std::string& error);
bool save_global_config(const std::string& path, const GlobalConfig& cfg, std::string& error);
bool parse_profile_text(const std::string& text, Profile& out, std::string& error);
bool load_profile(const std::string& path, Profile& out, std::string& error);
std::vector<std::string> list_profiles(const std::string& directory);

}  // namespace dji
