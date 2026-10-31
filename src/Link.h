#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Duml.h"
#include "RcState.h"
#include "Serial.h"

namespace dji {

struct LinkStats {
    uint64_t polls_sent = 0;
    uint64_t channel_frames = 0;
    uint64_t button_frames = 0;
    uint64_t status_frames = 0;
    uint64_t other_frames = 0;
    uint64_t timeouts = 0;
    uint64_t consecutive_timeouts = 0;
    uint64_t resyncs = 0;
    uint64_t frames_ok = 0;
    uint64_t frames_bad = 0;
    double last_rtt_ms = 0.0;
    double avg_rtt_ms = 0.0;
    double measured_hz = 0.0;
};

class RcLink {
public:
    struct Schedule {
        int channel_hz = 100;
        int button_hz = 50;
        int status_hz = 1;
        int response_timeout_ms = 10;
        int stale_reset_timeouts = 40;
    };

    ~RcLink();

    // `preferred_port` may be empty (auto-detect the highest-scoring DJI port).
    bool connect(const std::string& preferred_port, Schedule schedule, std::string& error);
    void disconnect();
    bool connected() const { return serial_.is_open(); }
    const std::string& port() const { return port_; }
    const LinkStats& stats() const { return stats_; }

    // One iteration of the poll schedule; updates `state`. False means the link died.
    bool step(RawState& state);

    // Sends an arbitrary command id and collects whatever answers (discovery aid).
    std::vector<Frame> probe(uint8_t cmd_id, const uint8_t* payload, size_t payload_len,
                             int wait_ms);

    // Picks the interface that actually answers DUML. Windows often names both CDC
    // interfaces "USB 串行设备", so the friendly name alone cannot tell protocol from debug.
    static std::string detect_port(int wait_ms = 300, const std::string& preferred = {});

private:
    bool send(uint8_t cmd_id, const uint8_t* payload, size_t payload_len);
    void ingest(RawState& state, double deadline_at);
    void handle(const Frame& frame, RawState& state, double now);

    SerialPort serial_;
    FrameDecoder decoder_;
    std::string port_;
    Schedule schedule_{};
    LinkStats stats_{};
    uint16_t seq_ = 0x34EB;
    double next_channel_at_ = 0.0;
    double next_button_at_ = 0.0;
    double next_status_at_ = 0.0;
    double request_started_at_ = 0.0;
    bool awaiting_channel_ = false;
    double rate_window_start_ = 0.0;
    uint64_t rate_window_frames_ = 0;
};

}  // namespace dji
