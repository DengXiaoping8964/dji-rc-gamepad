#pragma once
#include <cstdint>

namespace dji {

// Analogue channels as reported by the 0x01 packet, in DUML field order.
enum AnalogId : int {
    kAnaLeftX = 0,  // yaw
    kAnaLeftY = 1,  // throttle
    kAnaRightX = 2, // roll
    kAnaRightY = 3, // pitch
    kAnaDial = 4,   // gimbal pitch wheel
    kAnaCount = 5
};

extern const char* const kAnalogNames[kAnaCount];

struct AxisCal {
    int minimum = 364;
    int center = 1024;
    int maximum = 1684;
    bool invert = false;
    double deadzone = 0.08;
    double expo = 0.30;  // 0 = linear, 1 = fully cubic

    bool valid() const { return maximum > center + 20 && center > minimum + 20; }
};

// Maps a raw channel reading to -1..1 with centre, end-stop scaling, invert and
// deadzone applied. `apply_expo` adds the aiming curve; digital thresholds use the
// linear form so "pushed 80%" means 80% of physical travel regardless of expo.
double normalise_axis(const AxisCal& cal, int raw, bool apply_expo = true);

struct RawState {
    int raw[kAnaCount] = {1024, 1024, 1024, 1024, 1024};
    uint8_t button_bits = 0;
    uint8_t flight_mode = 0x10;  // normal until the RC tells otherwise
    int battery = -1;
    double channels_at = 0.0;
    double buttons_at = 0.0;
    bool has_channels = false;
    bool has_buttons = false;

    bool button(uint8_t mask) const { return (button_bits & mask) != 0; }
    bool stale(double now, double limit_seconds) const {
        return !has_channels || (now - channels_at) > limit_seconds;
    }
    void center_all() {
        for (int& value : raw) {
            value = 1024;
        }
    }
};

}  // namespace dji
