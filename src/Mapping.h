#pragma once
#include <string>
#include <vector>

#include "Config.h"
#include "RcState.h"

namespace dji {

struct PadOutput {
    uint16_t buttons = 0;
    uint8_t lt = 0;
    uint8_t rt = 0;
    int16_t lx = 0;
    int16_t ly = 0;
    int16_t rx = 0;
    int16_t ry = 0;

    void clear() { *this = PadOutput{}; }
    bool idle() const;
    std::string describe() const;
};

// Turns the remote's raw state into a full Xbox 360 report: analogue sticks,
// analogue triggers (from the single gimbal wheel), d-pad and a hold-to-shift
// layer for every button the remote physically lacks.
class Mapper {
public:
    void configure(const Profile& profile, const GlobalConfig& config);
    void update(const RawState& state, double now, PadOutput& out);
    void reset();

    // Human-readable diagnostics for --watch.
    std::string explain(const RawState& state, double now);
    const std::vector<std::string>& unknown_sources() const { return unknown_; }

private:
    struct ModifierState {
        std::string name;
        std::string source;
        double active_since = -1.0;
        bool engaged = false;
    };

    void refresh(const RawState& state, double now);
    double analog_of(const std::string& expr) const;
    double linear_of(const std::string& expr) const;
    bool digital_of(const std::string& expr, const RawState& state) const;
    void note_unknown(const std::string& text) const;
    bool modifier_engaged(const std::string& name) const;
    bool any_modifier_engaged() const;

    Profile profile_{};
    GlobalConfig config_{};
    double norm_[kAnaCount] = {0, 0, 0, 0, 0};
    double lin_[kAnaCount] = {0, 0, 0, 0, 0};
    double dial_pos_ = 0.0;
    int dial_last_raw_ = 1024;
    double dial_last_move_at_ = -1.0;
    double last_update_at_ = -1.0;
    bool dial_seeded_ = false;
    bool dial_moved_ = false;
    std::vector<ModifierState> modifiers_;
    mutable std::vector<std::string> unknown_;
};

}  // namespace dji
