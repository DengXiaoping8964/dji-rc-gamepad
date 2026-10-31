#include "RcState.h"

#include <algorithm>
#include <cmath>

namespace dji {

const char* const kAnalogNames[kAnaCount] = {"left_x", "left_y", "right_x", "right_y", "dial"};

double normalise_axis(const AxisCal& cal, int raw, bool apply_expo) {
    const double offset = static_cast<double>(raw) - static_cast<double>(cal.center);
    const double half = offset >= 0.0 ? static_cast<double>(cal.maximum - cal.center)
                                      : static_cast<double>(cal.center - cal.minimum);
    double x = offset / (half > 1.0 ? half : 660.0);
    x = std::clamp(x, -1.0, 1.0);
    if (cal.invert) {
        x = -x;
    }
    const double dz = std::clamp(cal.deadzone, 0.0, 0.9);
    const double magnitude = std::abs(x);
    if (magnitude <= dz) {
        return 0.0;
    }
    const double scaled = (magnitude - dz) / (1.0 - dz);
    x = x < 0.0 ? -scaled : scaled;
    if (!apply_expo) {
        return x;
    }
    const double e = std::clamp(cal.expo, 0.0, 1.0);
    const double shaped = (1.0 - e) * x + e * x * x * x;
    return std::clamp(shaped, -1.0, 1.0);
}

}  // namespace dji
