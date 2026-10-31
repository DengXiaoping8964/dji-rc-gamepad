#include "Mapping.h"

#include <windows.h>

#include <ViGEm/Common.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Duml.h"
#include "Util.h"

namespace dji {
namespace {

struct SlotMask {
    const char* slot;
    uint16_t mask;
};

const SlotMask kDigitalSlots[] = {
    {"A", XUSB_GAMEPAD_A},
    {"B", XUSB_GAMEPAD_B},
    {"X", XUSB_GAMEPAD_X},
    {"Y", XUSB_GAMEPAD_Y},
    {"LB", XUSB_GAMEPAD_LEFT_SHOULDER},
    {"RB", XUSB_GAMEPAD_RIGHT_SHOULDER},
    {"START", XUSB_GAMEPAD_START},
    {"BACK", XUSB_GAMEPAD_BACK},
    {"LS", XUSB_GAMEPAD_LEFT_THUMB},
    {"RS", XUSB_GAMEPAD_RIGHT_THUMB},
    {"DPAD_UP", XUSB_GAMEPAD_DPAD_UP},
    {"DPAD_DOWN", XUSB_GAMEPAD_DPAD_DOWN},
    {"DPAD_LEFT", XUSB_GAMEPAD_DPAD_LEFT},
    {"DPAD_RIGHT", XUSB_GAMEPAD_DPAD_RIGHT},
};

uint16_t mask_of_slot(const std::string& slot) {
    for (const auto& entry : kDigitalSlots) {
        if (slot == entry.slot) {
            return entry.mask;
        }
    }
    return 0;
}

bool parse_comparison(const std::string& expr, std::string& axis, std::string& op,
                      double& value) {
    size_t pos = std::string::npos;
    std::string found;
    for (const char* candidate : {">=", "<=", "!", ">", "<"}) {
        const size_t hit = expr.find(candidate);
        if (hit != std::string::npos && (pos == std::string::npos || hit < pos)) {
            pos = hit;
            found = candidate;
        }
    }
    if (pos == std::string::npos) {
        return false;
    }
    axis = trim(expr.substr(0, pos));
    op = found;
    return parse_double(trim(expr.substr(pos + found.size())), value);
}

double scale_axis(double value) {
    return std::clamp(value, -1.0, 1.0);
}

}  // namespace

void Mapper::configure(const Profile& profile, const GlobalConfig& config) {
    profile_ = profile;
    config_ = config;
    modifiers_.clear();
    for (const auto& entry : profile.modifiers) {
        ModifierState state;
        state.name = entry.first;
        state.source = to_lower(entry.second);
        modifiers_.push_back(state);
    }
    reset();
}

void Mapper::reset() {
    dial_pos_ = 0.0;
    dial_last_raw_ = 1024;
    dial_last_move_at_ = -1.0;
    last_update_at_ = -1.0;
    dial_seeded_ = false;
    dial_moved_ = false;
    unknown_.clear();
    for (auto& modifier : modifiers_) {
        modifier.active_since = -1.0;
        modifier.engaged = false;
    }
}

void Mapper::refresh(const RawState& state, double now) {
    for (int i = 0; i < kAnaCount; ++i) {
        norm_[i] = normalise_axis(config_.axes[i], state.raw[i], true);
        lin_[i] = normalise_axis(config_.axes[i], state.raw[i], false);
    }

    const double dt = last_update_at_ < 0.0 ? 0.008 : std::clamp(now - last_update_at_, 0.0, 0.1);
    last_update_at_ = now;

    const int raw_dial = state.raw[kAnaDial];
    if (!dial_seeded_) {
        dial_last_raw_ = raw_dial;
        dial_seeded_ = true;
        dial_last_move_at_ = now;
    }
    if (config_.dial.mode == "absolute") {
        dial_pos_ = norm_[kAnaDial];
        dial_moved_ = std::abs(dial_pos_) > 0.05;
    } else {
        int delta = raw_dial - dial_last_raw_;
        dial_last_raw_ = raw_dial;
        // A lost frame or an encoder wrap must not slam the trigger to full.
        delta = std::clamp(delta, -128, 128);
        dial_moved_ = delta != 0;
        if (delta != 0) {
            dial_pos_ += static_cast<double>(delta) * config_.dial.gain / 100.0;
            dial_last_move_at_ = now;
        }
        if ((now - dial_last_move_at_) * 1000.0 >= static_cast<double>(config_.dial.decay_ms)) {
            const double step = config_.dial.release_per_sec * dt;
            if (std::abs(dial_pos_) <= step) {
                dial_pos_ = 0.0;
            } else {
                dial_pos_ -= dial_pos_ < 0.0 ? -step : step;
            }
        }
        dial_pos_ = std::clamp(dial_pos_, -1.0, 1.0);
    }

    for (auto& modifier : modifiers_) {
        const bool active = digital_of(modifier.source, state);
        if (!active) {
            modifier.active_since = -1.0;
            modifier.engaged = false;
            continue;
        }
        if (modifier.active_since < 0.0) {
            modifier.active_since = now;
        }
        modifier.engaged = (now - modifier.active_since) * 1000.0 >=
                           static_cast<double>(profile_.hold_ms);
    }
}

double Mapper::analog_of(const std::string& expr) const {
    if (expr == "stick.lx") return scale_axis(norm_[kAnaLeftX]);
    if (expr == "stick.ly") return scale_axis(norm_[kAnaLeftY]);
    if (expr == "stick.rx") return scale_axis(norm_[kAnaRightX]);
    if (expr == "stick.ry") return scale_axis(norm_[kAnaRightY]);
    if (expr == "dial") return std::clamp(dial_pos_, -1.0, 1.0);
    if (expr == "dial.pos") return std::max(0.0, std::min(1.0, dial_pos_));
    if (expr == "dial.neg") return std::max(0.0, std::min(1.0, -dial_pos_));
    if (expr == "dial.absolute") return scale_axis(norm_[kAnaDial]);
    if (expr == "zero") return 0.0;
    if (expr == "one") return 1.0;
    if (expr.rfind("const.", 0) == 0) {
        double value = 0.0;
        if (parse_double(expr.substr(6), value)) {
            return std::clamp(value, 0.0, 1.0);
        }
    }
    note_unknown("source:" + expr);
    return 0.0;
}

// Digital thresholds read the linear (expo-free) value so that ">0.8" always means
// 80% of the physical stick travel.
double Mapper::linear_of(const std::string& expr) const {
    if (expr == "stick.lx") return lin_[kAnaLeftX];
    if (expr == "stick.ly") return lin_[kAnaLeftY];
    if (expr == "stick.rx") return lin_[kAnaRightX];
    if (expr == "stick.ry") return lin_[kAnaRightY];
    if (expr == "dial" || expr == "dial.pos" || expr == "dial.neg") return analog_of(expr);
    return analog_of(expr);
}

bool Mapper::digital_of(const std::string& expr, const RawState& state) const {    if (expr == "btn.fn") return state.button(kBitFn);
    if (expr == "btn.pv") return state.button(kBitPhotoVideo);
    if (expr == "btn.rec") return state.button(kBitRecord);
    if (expr == "btn.rth") return state.button(kBitRth);
    if (expr == "mode.sport") return state.flight_mode == kModeSport;
    if (expr == "mode.normal") return state.flight_mode == kModeNormal;
    if (expr == "mode.cine") return state.flight_mode == kModeCine;
    if (expr == "always") return true;
    if (expr == "never") return false;
    if (expr == "dial.moved") return dial_moved_;

    std::string axis;
    std::string op;
    double value = 0.0;
    if (parse_comparison(expr, axis, op, value)) {
        const double sample = linear_of(axis);
        if (op == ">") return sample > value;
        if (op == ">=") return sample >= value;
        if (op == "<") return sample < value;
        if (op == "<=") return sample <= value;
        if (op == "!") return std::abs(sample) > value;
    }
    note_unknown("source:" + expr);
    return false;
}

bool Mapper::any_modifier_engaged() const {
    for (const auto& modifier : modifiers_) {
        if (modifier.engaged) {
            return true;
        }
    }
    return false;
}

void Mapper::note_unknown(const std::string& text) const {
    if (std::find(unknown_.begin(), unknown_.end(), text) == unknown_.end()) {
        unknown_.push_back(text);
    }
}

bool Mapper::modifier_engaged(const std::string& name) const {
    for (const auto& modifier : modifiers_) {
        if (modifier.name == name) {
            return modifier.engaged;
        }
    }
    return false;
}

void Mapper::update(const RawState& state, double now, PadOutput& out) {
    refresh(state, now);
    out.clear();

    // Exclusive layers, like a keyboard Fn layer: while any modifier is held the
    // base bindings are ignored entirely, so one physical key never fires twice.
    const bool layered = any_modifier_engaged();
    for (const auto& binding : profile_.digital) {
        const bool in_layer = !binding.modifier.empty();
        if (in_layer != layered) {
            continue;
        }
        if (in_layer && !modifier_engaged(binding.modifier)) {
            continue;
        }
        const uint16_t mask = mask_of_slot(binding.slot);
        if (mask == 0) {
            note_unknown("slot:" + binding.slot);
            continue;
        }
        if (digital_of(binding.source, state)) {
            out.buttons |= mask;
        }
    }

    for (const auto& slot : {"LX", "LY", "RX", "RY"}) {
        const Binding* binding = profile_.find_analog(slot);
        const double value = binding != nullptr ? analog_of(binding->source) : 0.0;
        const auto scaled = static_cast<int16_t>(std::lround(scale_axis(value) * 32767.0));
        if (slot[0] == 'L' && slot[1] == 'X') out.lx = scaled;
        else if (slot[0] == 'L' && slot[1] == 'Y') out.ly = scaled;
        else if (slot[0] == 'R' && slot[1] == 'X') out.rx = scaled;
        else out.ry = scaled;
    }

    const Binding* lt = profile_.find_analog("LT");
    const Binding* rt = profile_.find_analog("RT");
    out.lt = static_cast<uint8_t>(std::lround(std::clamp(lt ? analog_of(lt->source) : 0.0, 0.0, 1.0) * 255.0));
    out.rt = static_cast<uint8_t>(std::lround(std::clamp(rt ? analog_of(rt->source) : 0.0, 0.0, 1.0) * 255.0));
}

bool PadOutput::idle() const {
    return buttons == 0 && lt == 0 && rt == 0 && lx == 0 && ly == 0 && rx == 0 && ry == 0;
}

std::string PadOutput::describe() const {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "btn=%04X LT=%3u RT=%3u LX=%6d LY=%6d RX=%6d RY=%6d",
                  buttons, lt, rt, lx, ly, rx, ry);
    return buffer;
}

std::string Mapper::explain(const RawState& state, double now) {
    PadOutput probe;
    update(state, now, probe);
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer),
                  "axes lx=%+.2f ly=%+.2f rx=%+.2f ry=%+.2f dial=%+.2f | bits=%02X mode=%02X | %s",
                  norm_[kAnaLeftX], norm_[kAnaLeftY], norm_[kAnaRightX], norm_[kAnaRightY],
                  dial_pos_, state.button_bits, state.flight_mode, probe.describe().c_str());
    return buffer;
}

}  // namespace dji
