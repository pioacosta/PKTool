#pragma once

#include <cstddef>
#include <cstdint>

namespace pktool::util {

// CRC-16/CCITT-FALSE (polynomial 0x1021, initial value 0xFFFF, no reflection).
// Used by the Gen IV and Gen V save block checksums.
uint16_t crc16Ccitt(const uint8_t* data, size_t length);

// The same checksum computed in pieces: start from kCrc16Init, feed each piece, the result of the
// last call is the checksum (crc16Ccitt(d, n) == crc16CcittUpdate(kCrc16Init, d, n)).
constexpr uint16_t kCrc16Init = 0xFFFF;
uint16_t crc16CcittUpdate(uint16_t crc, const uint8_t* data, size_t length);

}  // namespace pktool::util
