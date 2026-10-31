#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dji {

// ---- DUML wire format (agreed by two independent community implementations) ----
//
//   off  field
//   0    0x55                      sync / start of frame
//   1    length low                total frame length
//   2    length high | version<<2  bits0-1 = length high, bits 10-15 = protocol version
//   3    header CRC-8              over bytes 0..2, seed 0x77
//   4    source                    0x0A = host application
//   5    target                    0x06 = remote controller
//   6-7  sequence                  little-endian
//   8    command type              0x40 = request
//   9    command set               0x06 = remote controller set
//   10   command id                see Cmd
//   11.. payload
//   N-2  packet CRC-16             over bytes 0..N-3, little-endian, seed 0x3692

constexpr uint8_t kSyncByte = 0x55;
constexpr size_t kMinFrameSize = 13;
constexpr size_t kMaxFrameSize = 0x3FF;
constexpr uint8_t kProtocolVersion = 0x01;
constexpr uint8_t kAddrHost = 0x0A;
constexpr uint8_t kAddrRemote = 0x06;
constexpr uint8_t kCmdTypeRequest = 0x40;
constexpr uint8_t kCmdSetRemote = 0x06;

enum Cmd : uint8_t {
    kCmdChannels = 0x01,       // stick + dial channels, 38-byte reply
    kCmdStatus = 0x1E,         // RC status incl. battery, 19-byte reply
    kCmdSimulatorMode = 0x24,  // payload 0x01 -> start streaming input
    kCmdButtons = 0x27,        // buttons + flight-mode switch, 58-byte reply
};

// Offsets inside the 38-byte channel reply. Axes are 16-bit little-endian,
// resting at 1024 and spanning roughly 364..1684; fields sit on a stride of 3.
enum ChannelOffset : size_t {
    kOffRightX = 13,  // roll
    kOffRightY = 16,  // pitch
    kOffLeftY = 19,   // throttle
    kOffLeftX = 22,   // yaw
    kOffDial = 25,    // gimbal pitch wheel
    kChannelReplySize = 38,
};

// Offsets inside the 58-byte button reply.
enum ButtonOffset : size_t {
    kOffFlightMode = 28,  // 0x00 sport, 0x10 normal, 0x20 cinematic
    kOffButtonBits = 29,
    kButtonReplySize = 58,
};

enum ButtonBits : uint8_t {
    kBitFn = 0x02,      // customisable function key
    kBitPhotoVideo = 0x04,
    kBitRecord = 0x60,  // shutter (two adjacent bits)
    kBitRth = 0x80,     // pause / return-to-home
};

enum FlightModeValue : uint8_t {
    kModeSport = 0x00,
    kModeNormal = 0x10,
    kModeCine = 0x20,
};

constexpr size_t kStatusBatteryOffset = 15;

struct Frame {
    std::array<uint8_t, kMaxFrameSize> data{};
    size_t size = 0;

    uint8_t at(size_t off) const { return off < size ? data[off] : 0; }
    uint16_t u16(size_t off) const {
        return off + 1 < size ? static_cast<uint16_t>(data[off] | (data[off + 1] << 8)) : 0;
    }
    int16_t i16(size_t off) const { return static_cast<int16_t>(u16(off)); }
    uint8_t source() const { return at(4); }
    uint8_t target() const { return at(5); }
    uint16_t sequence() const { return u16(6); }
    uint8_t cmd_type() const { return at(8); }
    uint8_t cmd_set() const { return at(9); }
    uint8_t cmd_id() const { return at(10); }
    size_t payload_size() const { return size > kMinFrameSize ? size - kMinFrameSize + 1 : 0; }
};

// Builds one request frame into `out`; returns the byte count, 0 on bad args.
size_t build_request(uint8_t* out, size_t cap, uint8_t cmd_id, const uint8_t* payload,
                     size_t payload_len, uint16_t sequence, uint8_t source = kAddrHost,
                     uint8_t target = kAddrRemote);

// Incremental stream decoder with CRC-verified resynchronisation.
class FrameDecoder {
public:
    void feed(const uint8_t* data, size_t len);
    bool pop(Frame& out);
    void reset();

    uint64_t frames_ok = 0;
    uint64_t frames_bad = 0;
    uint64_t bytes_dropped = 0;

private:
    std::vector<uint8_t> buf_;
};

// Raw axis reading (rest 1024) -> signed -32768..32767, as the community impl does.
inline int32_t normalise_axis(uint16_t raw) {
    const int32_t out = (static_cast<int32_t>(raw) - 1024) * 2 * 4096 / 165;
    return out >= 32768 ? 32767 : out;
}

}  // namespace dji
