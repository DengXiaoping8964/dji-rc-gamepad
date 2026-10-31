#pragma once
#include <cstddef>
#include <cstdint>

namespace dji {

// DJI DUML uses a reflected CRC-8 (poly 0x8C) for the 4-byte header and a
// reflected CRC-16 (poly 0x8408) for the whole packet. Both are table-driven.
uint8_t crc8_dji(const uint8_t* data, size_t len, uint8_t seed = 0x77);
uint16_t crc16_dji(const uint8_t* data, size_t len, uint16_t seed = 0x3692);

}  // namespace dji
