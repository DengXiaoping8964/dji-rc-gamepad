// Validates the output half of the bridge without any DJI hardware attached:
// creates a ViGEm Xbox 360 virtual pad, drives it through every input, and
// reports whether the installed bus driver accepts the vendored client.

#include <windows.h>

#include <ViGEm/Client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "Duml.h"
#include "Log.h"
#include "Serial.h"

namespace {

struct Perf {
    LARGE_INTEGER freq{};
    LARGE_INTEGER start{};
    Perf() {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
    }
    double seconds() const {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
    }
};

const char* vigem_error_name(VIGEM_ERROR error) {
    switch (error) {
        case VIGEM_ERROR_NONE: return "NONE";
        case VIGEM_ERROR_BUS_NOT_FOUND: return "BUS_NOT_FOUND";
        case VIGEM_ERROR_NO_FREE_SLOT: return "NO_FREE_SLOT";
        case VIGEM_ERROR_INVALID_TARGET: return "INVALID_TARGET";
        case VIGEM_ERROR_REMOVAL_FAILED: return "REMOVAL_FAILED";
        case VIGEM_ERROR_ALREADY_CONNECTED: return "ALREADY_CONNECTED";
        case VIGEM_ERROR_TARGET_UNINITIALIZED: return "TARGET_UNINITIALIZED";
        case VIGEM_ERROR_TARGET_NOT_PLUGGED_IN: return "TARGET_NOT_PLUGGED_IN";
        case VIGEM_ERROR_BUS_VERSION_MISMATCH: return "BUS_VERSION_MISMATCH";
        case VIGEM_ERROR_BUS_ACCESS_FAILED: return "BUS_ACCESS_FAILED";
        case VIGEM_ERROR_CALLBACK_ALREADY_REGISTERED: return "CALLBACK_ALREADY_REGISTERED";
        case VIGEM_ERROR_CALLBACK_NOT_FOUND: return "CALLBACK_NOT_FOUND";
        case VIGEM_ERROR_BUS_ALREADY_CONNECTED: return "BUS_ALREADY_CONNECTED";
        case VIGEM_ERROR_BUS_INVALID_HANDLE: return "BUS_INVALID_HANDLE";
        case VIGEM_ERROR_XUSB_USERINDEX_OUT_OF_RANGE: return "USERINDEX_OUT_OF_RANGE";
        case VIGEM_ERROR_INVALID_PARAMETER: return "INVALID_PARAMETER";
        case VIGEM_ERROR_NOT_SUPPORTED: return "NOT_SUPPORTED";
        case VIGEM_ERROR_WINAPI: return "WINAPI";
        case VIGEM_ERROR_TIMED_OUT: return "TIMED_OUT";
        case VIGEM_ERROR_IS_DISPOSING: return "IS_DISPOSING";
        default: return "OTHER";
    }
}

int print_ports() {
    const auto ports = dji::enumerate_ports();
    if (ports.empty()) {
        std::printf("未发现任何 COM 端口\n");
        return 1;
    }
    std::printf("%-8s %-6s %-10s %s\n", "PORT", "SCORE", "VID:PID", "FRIENDLY");
    for (const auto& port : ports) {
        char ids[32];
        std::snprintf(ids, sizeof(ids), "%04X:%04X", port.vid, port.pid);
        std::printf("%-8s %-6d %-10s %s\n", port.com.c_str(), port.score, ids,
                    port.friendly.c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    dji::log_set_level(dji::Level::kDebug);

    int seconds = 12;
    bool animate = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ports") == 0) {
            return print_ports();
        } else if (std::strcmp(argv[i], "--hold") == 0) {
            animate = false;
        } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
        }
    }

    // Protocol self-check: a known request frame must round-trip through the decoder.
    {
        uint8_t tx[dji::kMaxFrameSize];
        const uint8_t payload[] = {0x01};
        const size_t n = dji::build_request(tx, sizeof(tx), dji::kCmdSimulatorMode, payload, 1, 0x34EB);
        if (n != 14) {
            LOGE("协议自测: 期望 14 字节, 实际 %zu", n);
            return 3;
        }
        dji::FrameDecoder decoder;
        decoder.feed(tx, n);
        dji::Frame frame;
        if (!decoder.pop(frame) || frame.cmd_id() != dji::kCmdSimulatorMode ||
            frame.at(11) != 0x01 || frame.sequence() != 0x34EB) {
            LOGE("协议自测: 帧回环解析失败");
            return 3;
        }
        LOGI("协议自测: DUML 组帧/解析回环通过 (%zu 字节, crc8=%02X crc16=%02X%02X)", n, frame.at(3),
             frame.at(n - 2), frame.at(n - 1));
    }

    PVIGEM_CLIENT vigem = vigem_alloc();
    if (vigem == nullptr) {
        LOGE("vigem_alloc 失败");
        return 4;
    }
    const VIGEM_ERROR connected = vigem_connect(vigem);
    if (connected != VIGEM_ERROR_NONE) {
        LOGE("vigem_connect 失败: %s (0x%08X) — 已装驱动可能与该客户端不匹配",
             vigem_error_name(connected), static_cast<unsigned>(connected));
        vigem_free(vigem);
        return 5;
    }
    LOGI("vigem_connect 成功: 已打开虚拟手柄总线");

    PVIGEM_TARGET pad = vigem_target_x360_alloc();
    if (pad == nullptr) {
        LOGE("vigem_target_x360_alloc 失败");
        vigem_disconnect(vigem);
        vigem_free(vigem);
        return 6;
    }
    const VIGEM_ERROR added = vigem_target_add(vigem, pad);
    if (added != VIGEM_ERROR_NONE) {
        LOGE("vigem_target_add 失败: %s (0x%08X)", vigem_error_name(added),
             static_cast<unsigned>(added));
        vigem_target_free(pad);
        vigem_disconnect(vigem);
        vigem_free(vigem);
        return 7;
    }
    ULONG user_index = 0xFFFFFFFF;
    vigem_target_x360_get_user_index(vigem, pad, &user_index);
    LOGI("虚拟 Xbox360 手柄已创建: index=%lu user=%lu VID=%04X PID=%04X",
         vigem_target_get_index(pad), user_index, vigem_target_get_vid(pad),
         vigem_target_get_pid(pad));

    const char* buttons[] = {"A", "B", "X", "Y", "LB", "RB", "Start", "Back", "LS", "RS",
                             "Up", "Down", "Left", "Right"};
    const uint16_t masks[] = {XUSB_GAMEPAD_A,          XUSB_GAMEPAD_B,        XUSB_GAMEPAD_X,
                              XUSB_GAMEPAD_Y,          XUSB_GAMEPAD_LEFT_SHOULDER,
                              XUSB_GAMEPAD_RIGHT_SHOULDER, XUSB_GAMEPAD_START, XUSB_GAMEPAD_BACK,
                              XUSB_GAMEPAD_LEFT_THUMB, XUSB_GAMEPAD_RIGHT_THUMB,
                              XUSB_GAMEPAD_DPAD_UP,    XUSB_GAMEPAD_DPAD_DOWN,
                              XUSB_GAMEPAD_DPAD_LEFT,  XUSB_GAMEPAD_DPAD_RIGHT};
    static_assert(sizeof(buttons) / sizeof(*buttons) == sizeof(masks) / sizeof(*masks));

    Perf perf;
    const double period = 1.0 / 125.0;
    double next = 0.0;
    unsigned frame_count = 0;
    VIGEM_ERROR worst = VIGEM_ERROR_NONE;
    std::printf("正在驱动虚拟手柄 %.1f 秒 — 可用 joy.cpl / gamepad-tester.com 观察\n",
                static_cast<double>(seconds));
    while (perf.seconds() < seconds) {
        const double now = perf.seconds();
        if (now < next) {
            const long ms = static_cast<long>((next - now) * 1000.0);
            if (ms > 0) {
                Sleep(static_cast<DWORD>(ms));
            }
        }
        next += period;

        XUSB_REPORT report{};
        if (animate) {
            const double t = now * 1.5;
            report.sThumbLX = static_cast<SHORT>(std::lround(std::cos(t) * 30000.0));
            report.sThumbLY = static_cast<SHORT>(std::lround(std::sin(t) * 30000.0));
            report.sThumbRX = static_cast<SHORT>(std::lround(std::sin(t * 0.7) * 25000.0));
            report.sThumbRY = static_cast<SHORT>(std::lround(std::cos(t * 0.7) * 25000.0));
            report.bLeftTrigger = static_cast<BYTE>((std::sin(t * 2.0) * 0.5 + 0.5) * 255.0);
            report.bRightTrigger = static_cast<BYTE>(frame_count / 12 % 256);
            report.wButtons = masks[(frame_count / 25) % (sizeof(masks) / sizeof(*masks))];
        } else {
            report.sThumbLX = 12345;
            report.sThumbLY = -12345;
            report.wButtons = XUSB_GAMEPAD_A;
        }
        const VIGEM_ERROR updated = vigem_target_x360_update(vigem, pad, report);
        if (updated != VIGEM_ERROR_NONE) {
            worst = updated;
        }
        ++frame_count;
    }
    // Always leave the pad centred and released.
    XUSB_REPORT zero{};
    vigem_target_x360_update(vigem, pad, zero);

    LOGI("共发送 %u 帧 @125Hz, 最差返回码 %s (0x%08X)", frame_count,
         vigem_error_name(worst), static_cast<unsigned>(worst));

    vigem_target_remove(vigem, pad);
    vigem_target_free(pad);
    vigem_disconnect(vigem);
    vigem_free(vigem);
    LOGI("虚拟手柄已移除, 自测结束");
    return worst == VIGEM_ERROR_NONE ? 0 : 8;
}
