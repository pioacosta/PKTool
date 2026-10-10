#include "core/util/crc16.h"

namespace pktool::util {

uint16_t crc16Ccitt(const uint8_t* data, size_t length) {
    return crc16CcittUpdate(kCrc16Init, data, length);
}

uint16_t crc16CcittUpdate(uint16_t crc, const uint8_t* data, size_t length) {
    // Nibble-free byte-wise formulation; avoids a 512-byte lookup table.
    uint8_t top = static_cast<uint8_t>(crc >> 8);
    uint8_t bottom = static_cast<uint8_t>(crc);
    for (size_t i = 0; i < length; ++i) {
        uint8_t x = data[i] ^ top;
        x ^= x >> 4;
        top = static_cast<uint8_t>(bottom ^ (x >> 3) ^ (x << 4));
        bottom = static_cast<uint8_t>(x ^ (x << 5));
    }
    return static_cast<uint16_t>((top << 8) | bottom);
}

}  // namespace pktool::util
