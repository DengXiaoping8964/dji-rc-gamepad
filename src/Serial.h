#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dji {

struct PortInfo {
    std::string com;       // "COM7"
    std::string friendly;  // UTF-8
    std::string hwid;      // UTF-8, first hardware ID
    uint16_t vid = 0;
    uint16_t pid = 0;
    int score = 0;  // >0 means it looks like the DJI protocol interface

    bool looks_like_dji_protocol() const { return score >= 100; }
};

// Enumerates present COM ports with their PnP friendly names and ranks the ones
// that are the DJI "USB VCOM For Protocol" interface (never the "For Debug" one).
std::vector<PortInfo> enumerate_ports();

struct UsbAnomaly {
    std::string instance;    // e.g. USB\VID_0000&PID_0003\...
    std::string description; // UTF-8 friendly/device description
    unsigned long problem = 0;
    bool dji_vid = false;  // vendor 0x2CA3 seen in the instance path
};

// USB nodes that are either DJI's or currently failing enumeration; used to tell
// "remote not plugged in" apart from "remote visible but no serial interface".
std::vector<UsbAnomaly> enumerate_usb_anomalies();

class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // DTR/RTS are asserted by default, matching what pyserial does on Windows; some
    // CDC devices (DJI included) hold their TX until the control-line state says so.
    bool open(const std::string& com, uint32_t baud = 115200, bool dtr = true, bool rts = true);
    void close();
    bool is_open() const { return handle_ != invalid_handle; }

    // Flow-control lines. Some CDC devices only stream while DTR is asserted.
    void set_dtr(bool on);
    void set_rts(bool on);

    size_t write_all(const uint8_t* data, size_t len);
    size_t read_some(uint8_t* data, size_t cap);  // returns immediately, 0 if empty
    size_t in_waiting();
    void purge_all();

    const std::string& last_error() const { return last_error_; }

private:
    static constexpr void* invalid_handle = reinterpret_cast<void*>(-1);  // INVALID_HANDLE_VALUE
    void* handle_ = invalid_handle;
    std::string last_error_;
};

}  // namespace dji
