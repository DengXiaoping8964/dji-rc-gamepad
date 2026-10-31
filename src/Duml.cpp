#include "Duml.h"

#include <algorithm>

#include "Crc.h"

namespace dji {

size_t build_request(uint8_t* out, size_t cap, uint8_t cmd_id, const uint8_t* payload,
                     size_t payload_len, uint16_t sequence, uint8_t source, uint8_t target) {
    const size_t length = kMinFrameSize + payload_len;
    if (out == nullptr || cap < length || length > kMaxFrameSize) {
        return 0;
    }
    out[0] = kSyncByte;
    out[1] = static_cast<uint8_t>(length & 0xFFU);
    out[2] = static_cast<uint8_t>(((length >> 8) & 0x03U) | 0x04U);
    out[3] = crc8_dji(out, 3);
    out[4] = source;
    out[5] = target;
    out[6] = static_cast<uint8_t>(sequence & 0xFFU);
    out[7] = static_cast<uint8_t>(sequence >> 8);
    out[8] = kCmdTypeRequest;
    out[9] = kCmdSetRemote;
    out[10] = cmd_id;
    if (payload != nullptr && payload_len > 0) {
        std::copy_n(payload, payload_len, out + 11);
    }
    const uint16_t crc = crc16_dji(out, length - 2);
    out[length - 2] = static_cast<uint8_t>(crc & 0xFFU);
    out[length - 1] = static_cast<uint8_t>(crc >> 8);
    return length;
}

void FrameDecoder::feed(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
    if (buf_.size() > 8192) {  // runaway guard: keep the tail, it may hold a frame
        const size_t overflow = buf_.size() - 8192;
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(overflow));
        bytes_dropped += overflow;
    }
}

void FrameDecoder::reset() {
    buf_.clear();
}

bool FrameDecoder::pop(Frame& out) {
    while (!buf_.empty()) {
        const size_t sync = buf_.empty() ? 0 : [&] {
            const auto it = std::find(buf_.begin(), buf_.end(), kSyncByte);
            return it == buf_.end() ? buf_.size() : static_cast<size_t>(it - buf_.begin());
        }();
        if (sync > 0) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(sync));
            bytes_dropped += sync;
        }
        if (buf_.size() < 4) {
            return false;
        }
        const uint16_t length_word = static_cast<uint16_t>(buf_[1] | (buf_[2] << 8));
        const size_t length = length_word & kMaxFrameSize;
        const uint8_t version = static_cast<uint8_t>(length_word >> 10);
        const bool header_ok = crc8_dji(buf_.data(), 3) == buf_[3];
        if (!header_ok || version != kProtocolVersion || length < kMinFrameSize) {
            ++frames_bad;
            buf_.erase(buf_.begin());  // false sync: slide one byte and look again
            continue;
        }
        if (buf_.size() < length) {
            return false;  // frame still in flight
        }
        const uint16_t want = static_cast<uint16_t>(buf_[length - 2] | (buf_[length - 1] << 8));
        if (crc16_dji(buf_.data(), length - 2) != want) {
            ++frames_bad;
            buf_.erase(buf_.begin());
            continue;
        }
        out.size = length;
        std::copy_n(buf_.begin(), length, out.data.begin());
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<long>(length));
        ++frames_ok;
        return true;
    }
    return false;
}

}  // namespace dji
