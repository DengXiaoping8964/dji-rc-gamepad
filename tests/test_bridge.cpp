// Offline verification: DUML framing against golden vectors produced by the
// reference Python implementation, plus the mapping engine's layer/trigger logic.

#include <windows.h>

#include <ViGEm/Common.h>

#include <cstdio>
#include <string>
#include <vector>

#include "Config.h"
#include "Util.h"
#include "Duml.h"
#include "Mapping.h"
#include "RcState.h"

using namespace dji;

namespace {

int g_failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::printf("  FAIL 第%d行: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::string to_hex(const uint8_t* data, size_t len) {
    std::string out;
    char byte[4];
    for (size_t i = 0; i < len; ++i) {
        std::snprintf(byte, sizeof(byte), "%02X", data[i]);
        out += byte;
    }
    return out;
}

void test_framing() {
    std::printf("[1] DUML 组帧 vs 参考实现黄金样本\n");
    struct Vector {
        uint8_t cmd;
        const char* payload;
        const char* hex;
    };
    // Produced by DJI-RC-winGamePad/djirc/protocol.py with sequence 0x34EB.
    const Vector vectors[] = {
        {kCmdSimulatorMode, "\x01", "550E04660A06EB3440062401D9EC"},
        {kCmdChannels, nullptr, "550D04330A06EB344006017424"},
        {kCmdButtons, nullptr, "550D04330A06EB344006274060"},
        {kCmdStatus, nullptr, "550D04330A06EB3440061E02CC"},
    };
    for (const auto& vector : vectors) {
        uint8_t frame[kMaxFrameSize];
        const size_t len = build_request(frame, sizeof(frame), vector.cmd,
                                         reinterpret_cast<const uint8_t*>(vector.payload),
                                         vector.payload ? 1 : 0, 0x34EB);
        const std::string hex = to_hex(frame, len);
        const bool matches = hex == vector.hex;
        CHECK(matches);
        if (!matches) {
            std::printf("      cmd 0x%02X 期望 %s 实际 %s\n", vector.cmd, vector.hex, hex.c_str());
        }
    }
}

// Golden replies built with the reference framer: 38B channels, 58B buttons, 19B status.
const char* kChannelReply = "552604D1060A010040060100009406006C010000040014050000040000000000000000007469";
const char* kButtonReply =
    "553A0470060A01004006270000000000000000000000000000000000208200000000000000000000000000000000000000"
    "00000000000000D3EF";
const char* kStatusReply = "55130403060A010040061E000000004D00592C";

std::vector<uint8_t> unhex(const char* text) {
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::vector<uint8_t> bytes;
    const std::string hex(text);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
    }
    return bytes;
}

void test_decoding() {
    std::printf("[2] DUML 解帧、CRC 校验与重同步\n");
    FrameDecoder decoder;
    // Junk in front, and a fake sync byte inside the junk, must not produce frames.
    const uint8_t junk[] = {0x12, 0x55, 0xFF, 0x00, 0x55, 0x01};
    decoder.feed(junk, sizeof(junk));
    const auto channel = unhex(kChannelReply);
    decoder.feed(channel.data(), channel.size());
    Frame frame;
    CHECK(decoder.pop(frame));
    CHECK(frame.cmd_id() == kCmdChannels);
    CHECK(frame.size == kChannelReplySize);
    CHECK(frame.source() == kAddrRemote);
    CHECK(frame.u16(kOffRightX) == 1684);
    CHECK(frame.u16(kOffRightY) == 364);
    CHECK(frame.u16(kOffLeftY) == 1024);
    CHECK(frame.u16(kOffLeftX) == 1300);
    CHECK(frame.u16(kOffDial) == 1024);

    const auto buttons = unhex(kButtonReply);
    decoder.feed(buttons.data(), buttons.size());
    CHECK(decoder.pop(frame));
    CHECK(frame.cmd_id() == kCmdButtons);
    CHECK(frame.at(kOffFlightMode) == kModeCine);
    CHECK((frame.at(kOffButtonBits) & kBitRth) != 0);
    CHECK((frame.at(kOffButtonBits) & kBitFn) != 0);

    const auto status = unhex(kStatusReply);
    decoder.feed(status.data(), status.size());
    CHECK(decoder.pop(frame));
    CHECK(frame.cmd_id() == kCmdStatus);
    CHECK(frame.at(kStatusBatteryOffset) == 77);

    // Corrupt one byte of the payload: the CRC must reject it and re-sync.
    const auto again = unhex(kChannelReply);
    std::vector<uint8_t> broken = again;
    broken[14] ^= 0xFF;
    const uint64_t bad_before = decoder.frames_bad;
    decoder.feed(broken.data(), broken.size());
    Frame survivor;
    CHECK(!decoder.pop(survivor));  // nothing valid may come out of a corrupted frame
    CHECK(decoder.frames_bad > bad_before);
    decoder.feed(again.data(), again.size());
    CHECK(decoder.pop(survivor));
    CHECK(survivor.cmd_id() == kCmdChannels);
}

void test_axis_scaling() {
    std::printf("[3] 轴归一化、死区与曲线\n");
    AxisCal cal;  // 364 / 1024 / 1684, deadzone 0.08
    CHECK(normalise_axis(cal, 1024) == 0.0);
    CHECK(normalise_axis(cal, 1684) > 0.99);
    CHECK(normalise_axis(cal, 364) < -0.99);
    CHECK(std::abs(normalise_axis(cal, 1024 + 50)) < 0.05);  // inside the deadzone
    AxisCal linear = cal;
    linear.deadzone = 0.0;
    linear.expo = 0.0;
    CHECK(std::abs(normalise_axis(linear, 1354) - 0.5) < 0.001);
    AxisCal flipped = cal;
    flipped.invert = true;
    flipped.deadzone = 0.0;
    flipped.expo = 0.0;
    CHECK(normalise_axis(flipped, 1684) < -0.99);
}

const char* kTestProfile = R"INI(
[meta]
name = unit

[hold]
ms = 300

[modifiers]
L = btn.fn
R = btn.rth

[pad]
A = btn.rec
X = btn.pv
B = btn.rth
Y = btn.fn
LB = mode.sport
RB = mode.normal
BACK = mode.cine
START.L = btn.rec
DPAD_RIGHT.L = stick.rx>0.8
LT_BOGUS = btn.rec

[axis]
LX = stick.lx
LY = stick.ly
RX = stick.rx
RY = stick.ry
LT = dial.neg
RT = dial.pos
)INI";

RawState make_state(uint8_t bits, uint8_t mode, int left_x = 1024, int left_y = 1024,
                    int right_x = 1024, int right_y = 1024, int dial = 1024) {
    RawState state;
    state.button_bits = bits;
    state.flight_mode = mode;
    state.raw[kAnaLeftX] = left_x;
    state.raw[kAnaLeftY] = left_y;
    state.raw[kAnaRightX] = right_x;
    state.raw[kAnaRightY] = right_y;
    state.raw[kAnaDial] = dial;
    state.has_channels = true;
    state.has_buttons = true;
    return state;
}

void test_mapping() {
    std::printf("[4] 映射引擎：基础层 / 长按层 / 拨轮双扳机\n");
    Profile profile;
    std::string error;
    CHECK(parse_profile_text(kTestProfile, profile, error));
    if (!error.empty()) {
        std::printf("      解析错误: %s\n", error.c_str());
    }
    CHECK(profile.digital.size() == 10);
    CHECK(profile.modifiers.size() == 2);

    GlobalConfig config;
    Mapper mapper;
    mapper.configure(profile, config);

    const double t0 = 1000.0;
    PadOutput out;

    // Idle remote: nothing pressed.
    mapper.update(make_state(0x00, kModeNormal), t0, out);
    CHECK(out.buttons == XUSB_GAMEPAD_RIGHT_SHOULDER);  // N 档 -> RB
    CHECK(out.rt == 0 && out.lt == 0);

    // Shutter -> A, and the flight switch positions -> LB / BACK.
    mapper.update(make_state(kBitRecord, kModeSport), t0 + 0.01, out);
    CHECK(out.buttons == (XUSB_GAMEPAD_A | XUSB_GAMEPAD_LEFT_SHOULDER));
    mapper.update(make_state(0x00, kModeCine), t0 + 0.02, out);
    CHECK(out.buttons == XUSB_GAMEPAD_BACK);

    // A short tap on the custom key is still Y.
    mapper.update(make_state(kBitFn, kModeNormal), t0 + 0.03, out);
    CHECK(out.buttons == (XUSB_GAMEPAD_Y | XUSB_GAMEPAD_RIGHT_SHOULDER));

    // Held past the threshold it becomes the L layer: START wins, base keys drop out.
    mapper.update(make_state(kBitFn, kModeNormal), t0 + 0.05, out);
    mapper.update(make_state(kBitFn, kModeNormal), t0 + 0.10, out);
    mapper.update(make_state(kBitFn, kModeNormal), t0 + 0.20, out);
    mapper.update(make_state(kBitFn, kModeNormal), t0 + 0.40, out);
    CHECK((out.buttons & XUSB_GAMEPAD_Y) == 0);
    mapper.update(make_state(kBitFn | kBitRecord, kModeNormal), t0 + 0.41, out);
    CHECK(out.buttons == XUSB_GAMEPAD_START);

    // Stick-flick d-pad inside the layer.
    mapper.update(make_state(kBitFn, kModeNormal, 1024, 1024, 1600, 1024), t0 + 0.42, out);
    CHECK(out.buttons == XUSB_GAMEPAD_DPAD_RIGHT);

    // Releasing the modifier returns to the base layer.
    mapper.update(make_state(kBitRecord, kModeNormal), t0 + 0.50, out);
    CHECK(out.buttons == (XUSB_GAMEPAD_A | XUSB_GAMEPAD_RIGHT_SHOULDER));

    // The wheel splits into two analogue triggers: forward = RT, back = LT.
    int dial = 1024;
    for (int step = 0; step < 6; ++step) {
        dial += 12;
        mapper.update(make_state(0x00, kModeNormal, 1024, 1024, 1024, 1024, dial), t0 + 0.6 + step * 0.01,
                      out);
    }
    CHECK(out.rt > 60);
    CHECK(out.lt == 0);
    for (int step = 0; step < 20; ++step) {
        dial -= 12;
        mapper.update(make_state(0x00, kModeNormal, 1024, 1024, 1024, 1024, dial), t0 + 0.7 + step * 0.01,
                      out);
    }
    CHECK(out.lt > 60);
    CHECK(out.rt == 0);
    // Idle wheel releases both triggers again.
    for (int step = 0; step < 60; ++step) {
        mapper.update(make_state(0x00, kModeNormal, 1024, 1024, 1024, 1024, dial), t0 + 1.0 + step * 0.01,
                      out);
    }
    CHECK(out.lt == 0);
    CHECK(out.rt == 0);

    // Sticks reach full scale on the axes.
    mapper.update(make_state(0x00, kModeNormal, 1684, 364, 1684, 364), t0 + 2.0, out);
    CHECK(out.lx > 32000);
    CHECK(out.ly < -32000);
    CHECK(out.rx > 32000);
    CHECK(out.ry < -32000);

    const auto& unknown = mapper.unknown_sources();
    CHECK(!unknown.empty());  // LT_BOGUS must be reported, not silently dropped
    for (const auto& entry : unknown) {
        std::printf("      已识别的非法写法: %s\n", entry.c_str());
    }
}

void test_config_roundtrip() {
    std::printf("[5] 配置文件写出/读回\n");
    GlobalConfig config;
    config.port = "COM9";
    config.schedule.channel_hz = 90;
    config.dial.mode = "absolute";
    config.axes[kAnaLeftY].center = 1011;
    config.axes[kAnaLeftY].minimum = 400;
    config.axes[kAnaLeftY].maximum = 1700;
    config.axes[kAnaRightY].invert = true;
    config.auto_profile = false;

    const std::string path = exe_directory() + "\\test-roundtrip.ini";
    std::string error;
    CHECK(save_global_config(path, config, error));
    GlobalConfig reloaded;
    CHECK(load_global_config(path, reloaded, error));
    CHECK(reloaded.port == "COM9");
    CHECK(reloaded.schedule.channel_hz == 90);
    CHECK(reloaded.dial.mode == "absolute");
    CHECK(reloaded.axes[kAnaLeftY].center == 1011);
    CHECK(reloaded.axes[kAnaLeftY].maximum == 1700);
    CHECK(reloaded.axes[kAnaRightY].invert);
    CHECK(!reloaded.auto_profile);
    DeleteFileW(utf8_to_wide(path).c_str());
}

}  // namespace

int main() {
    SetConsoleOutputCP(CP_UTF8);
    test_framing();
    test_decoding();
    test_axis_scaling();
    test_mapping();
    test_config_roundtrip();
    if (g_failures == 0) {
        std::printf("\n全部通过 (0 failures)\n");
        return 0;
    }
    std::printf("\n失败 %d 项\n", g_failures);
    return 1;
}
