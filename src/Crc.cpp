#include "Crc.h"

#include <array>

namespace dji {
namespace {

std::array<uint8_t, 256> build_crc8() {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint8_t v = static_cast<uint8_t>(i);
        for (int b = 0; b < 8; ++b) {
            v = static_cast<uint8_t>((v & 1U) ? ((v >> 1) ^ 0x8CU) : (v >> 1));
        }
        table[static_cast<size_t>(i)] = v;
    }
    return table;
}

std::array<uint16_t, 256> build_crc16() {
    std::array<uint16_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint16_t v = static_cast<uint16_t>(i);
        for (int b = 0; b < 8; ++b) {
            v = static_cast<uint16_t>((v & 1U) ? ((v >> 1) ^ 0x8408U) : (v >> 1));
        }
        table[static_cast<size_t>(i)] = v;
    }
    return table;
}

const std::array<uint8_t, 256> kCrc8 = build_crc8();
const std::array<uint16_t, 256> kCrc16 = build_crc16();

}  // namespace

uint8_t crc8_dji(const uint8_t* data, size_t len, uint8_t seed) {
    uint8_t v = seed;
    for (size_t i = 0; i < len; ++i) {
        v = static_cast<uint8_t>(kCrc8[static_cast<size_t>(data[i] ^ v)]);
    }
    return v;
}

uint16_t crc16_dji(const uint8_t* data, size_t len, uint16_t seed) {
    uint16_t v = seed;
    for (size_t i = 0; i < len; ++i) {
        v = static_cast<uint16_t>((v >> 8) ^ kCrc16[static_cast<size_t>((data[i] ^ v) & 0xFFU)]);
    }
    return v;
}

}  // namespace dji
