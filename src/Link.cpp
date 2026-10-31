#include "Link.h"

#include <algorithm>
#include <cstring>

#include "Log.h"
#include "Util.h"

namespace dji {
namespace {

constexpr int kChannelStrideOffsets[kAnaCount] = {kOffRightX, kOffRightY, kOffLeftY, kOffLeftX,
                                                 kOffDial};

}  // namespace

RcLink::~RcLink() { disconnect(); }

std::string RcLink::detect_port(int wait_ms, const std::string& preferred) {
    const auto ports = enumerate_ports();
    std::vector<const PortInfo*> order;
    if (!preferred.empty()) {
        for (const auto& port : ports) {
            if (port.com == preferred) {
                order.push_back(&port);
            }
        }
    }
    for (const auto& port : ports) {
        if (port.score >= 100) {
            order.insert(order.begin(), &port);  // explicitly named "For Protocol" first
        }
    }
    for (const auto& port : ports) {
        if (port.score > 0 && port.score < 100) {
            bool seen = false;
            for (const auto* chosen : order) {
                seen = seen || chosen->com == port.com;
            }
            if (!seen) {
                order.push_back(&port);
            }
        }
    }
    for (const PortInfo* port : order) {
        RcLink probe_link;
        Schedule schedule;
        schedule.channel_hz = 60;
        schedule.button_hz = 10;
        std::string ignored;
        if (!probe_link.connect(port->com, schedule, ignored)) {
            continue;
        }
        RawState state;
        const double until = now_seconds() + wait_ms / 1000.0;
        while (now_seconds() < until) {
            if (!probe_link.step(state)) {
                break;
            }
            if (probe_link.stats().channel_frames > 0) {
                LOGI("端口 %s 用 DUML 实测应答 (%llu 个通道包)", port->com.c_str(),
                     static_cast<unsigned long long>(probe_link.stats().channel_frames));
                return port->com;
            }
        }
        LOGD("端口 %s 不应答 DUML, 跳过", port->com.c_str());
    }
    return {};
}

bool RcLink::connect(const std::string& preferred_port, Schedule schedule, std::string& error) {
    disconnect();
    schedule_ = schedule;

    std::string name = preferred_port;
    if (name.empty()) {
        name = detect_port();
        if (name.empty()) {
            error = "未找到应答 DUML 的 DJI 端口：请确认遥控器已开机并插在**充电/调参 USB-C 口**（不是接手机那个口）";
            return false;
        }
    }

    if (!serial_.open(name, 115200)) {
        error = "打开 " + name + " 失败: " + serial_.last_error();
        return false;
    }
    port_ = name;
    serial_.set_dtr(true);
    serial_.purge_all();
    decoder_.reset();

    // Ask the RC to stream input, exactly like the community bridges do.
    const uint8_t on = 0x01;
    if (!send(kCmdSimulatorMode, &on, 1)) {
        error = "已进入端口但无法写入模拟器模式命令";
        disconnect();
        return false;
    }
    const double now = now_seconds();
    next_channel_at_ = now;
    next_button_at_ = now;
    next_status_at_ = now;
    awaiting_channel_ = false;
    rate_window_start_ = now;
    rate_window_frames_ = 0;
    stats_ = LinkStats{};
    LOGI("串口 %s @115200 8N1 已打开, 已发送模拟器模式开启命令", port_.c_str());
    return true;
}

void RcLink::disconnect() {
    if (serial_.is_open()) {
        // Leave the remote in its normal state so DJI Fly keeps working afterwards.
        const uint8_t off = 0x00;
        send(kCmdSimulatorMode, &off, 1);
        serial_.close();
    }
    port_.clear();
    decoder_.reset();
}

bool RcLink::send(uint8_t cmd_id, const uint8_t* payload, size_t payload_len) {
    uint8_t frame[kMaxFrameSize];
    const size_t len = build_request(frame, sizeof(frame), cmd_id, payload, payload_len, seq_);
    if (len == 0) {
        return false;
    }
    seq_ = static_cast<uint16_t>(seq_ + 1);
    return serial_.write_all(frame, len) == len;
}

bool RcLink::step(RawState& state) {
    const double now = now_seconds();
    bool wrote = true;

    if (now >= next_channel_at_) {
        request_started_at_ = now;
        awaiting_channel_ = true;
        wrote = send(kCmdChannels, nullptr, 0);
        if (wrote) {
            ++stats_.polls_sent;
            next_channel_at_ = now + 1.0 / static_cast<double>(std::max(1, schedule_.channel_hz));
        }
    }
    if (now >= next_button_at_) {
        wrote = send(kCmdButtons, nullptr, 0) && wrote;
        if (wrote) {
            next_button_at_ = now + 1.0 / static_cast<double>(std::max(1, schedule_.button_hz));
        }
    }
    if (now >= next_status_at_) {
        wrote = send(kCmdStatus, nullptr, 0) && wrote;
        if (wrote) {
            next_status_at_ = now + 1.0 / static_cast<double>(std::max(1, schedule_.status_hz));
        }
    }
    if (!wrote) {
        LOGW("串口写入失败, 链路判定为断开");
        return false;
    }

    ingest(state, now + schedule_.response_timeout_ms / 1000.0);

    if (awaiting_channel_) {
        ++stats_.timeouts;
        ++stats_.consecutive_timeouts;
        if (stats_.consecutive_timeouts >= static_cast<uint64_t>(schedule_.stale_reset_timeouts)) {
            // The RC may have dropped out of streaming mode; re-arm it.
            LOGW("连续 %llu 次无通道响应, 重新发送模拟器模式开启命令",
                 static_cast<unsigned long long>(stats_.consecutive_timeouts));
            const uint8_t on = 0x01;
            send(kCmdSimulatorMode, &on, 1);
            stats_.consecutive_timeouts = 0;
            ++stats_.resyncs;
        }
    }

    const double window = now - rate_window_start_;
    if (window >= 1.0) {
        stats_.measured_hz = static_cast<double>(rate_window_frames_) / window;
        rate_window_frames_ = 0;
        rate_window_start_ = now;
    }
    return true;
}

void RcLink::ingest(RawState& state, double deadline_at) {
    uint8_t buffer[512];
    Frame frame;
    while (true) {
        const size_t got = serial_.read_some(buffer, sizeof(buffer));
        if (got > 0) {
            decoder_.feed(buffer, got);
            while (decoder_.pop(frame)) {
                handle(frame, state, now_seconds());
            }
            stats_.frames_bad = decoder_.frames_bad;
            stats_.frames_ok = decoder_.frames_ok;
        }
        if (!awaiting_channel_ || now_seconds() >= deadline_at) {
            break;
        }
    }
}

void RcLink::handle(const Frame& frame, RawState& state, double now) {
    const uint8_t id = frame.cmd_id();
    if (id == kCmdChannels &&
        (frame.size == kChannelReplySize || frame.size == 32)) {
        const size_t base = frame.size == kChannelReplySize ? kOffRightX : 12;
        state.raw[kAnaRightX] = frame.u16(base);
        state.raw[kAnaRightY] = frame.u16(base + 3);
        state.raw[kAnaLeftY] = frame.u16(base + 6);
        state.raw[kAnaLeftX] = frame.u16(base + 9);
        state.raw[kAnaDial] = frame.u16(base + 12);
        state.channels_at = now;
        state.has_channels = true;
        ++stats_.channel_frames;
        ++rate_window_frames_;
        stats_.consecutive_timeouts = 0;
        if (awaiting_channel_) {
            const double rtt = (now - request_started_at_) * 1000.0;
            stats_.last_rtt_ms = rtt;
            stats_.avg_rtt_ms =
                stats_.avg_rtt_ms == 0.0 ? rtt : (stats_.avg_rtt_ms * 0.9 + rtt * 0.1);
            awaiting_channel_ = false;
        }
    } else if (id == kCmdButtons && frame.size >= kButtonReplySize) {
        state.button_bits = frame.at(kOffButtonBits);
        state.flight_mode = frame.at(kOffFlightMode);
        state.buttons_at = now;
        state.has_buttons = true;
        ++stats_.button_frames;
    } else if (id == kCmdStatus && frame.size > kStatusBatteryOffset) {
        state.battery = std::min(100, static_cast<int>(frame.at(kStatusBatteryOffset)));
        ++stats_.status_frames;
    } else {
        ++stats_.other_frames;
        LOGT("未识别帧 cmd=0x%02X set=0x%02X len=%zu", id, frame.cmd_set(), frame.size);
    }
}

std::vector<Frame> RcLink::probe(uint8_t cmd_id, const uint8_t* payload, size_t payload_len,
                                 int wait_ms) {
    std::vector<Frame> replies;
    if (!serial_.is_open()) {
        return replies;
    }
    decoder_.reset();
    serial_.purge_all();
    if (!send(cmd_id, payload, payload_len)) {
        return replies;
    }
    const double deadline = now_seconds() + wait_ms / 1000.0;
    uint8_t buffer[512];
    Frame frame;
    while (now_seconds() < deadline) {
        const size_t got = serial_.read_some(buffer, sizeof(buffer));
        if (got > 0) {
            decoder_.feed(buffer, got);
            while (decoder_.pop(frame)) {
                replies.push_back(frame);
            }
        }
    }
    return replies;
}

}  // namespace dji
