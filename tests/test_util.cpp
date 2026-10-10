#include <cstring>

#include "core/util/bytes.h"
#include "core/util/crc16.h"
#include "tests/test.h"

using namespace pktool;

TEST(crc16_matches_ccitt_false_check_value) {
    const char* input = "123456789";
    CHECK_EQ(util::crc16Ccitt(reinterpret_cast<const uint8_t*>(input), std::strlen(input)), 0x29B1);
}

TEST(crc16_of_empty_input_is_initial_value) {
    CHECK_EQ(util::crc16Ccitt(nullptr, 0), 0xFFFF);
}

TEST(bytes_round_trip_little_endian) {
    uint8_t buffer[6] = {};
    util::writeU16(buffer, 0xBEEF);
    util::writeU32(buffer + 2, 0x12345678);
    CHECK_EQ(buffer[0], 0xEF);
    CHECK_EQ(buffer[2], 0x78);
    CHECK_EQ(buffer[5], 0x12);
    CHECK_EQ(util::readU16(buffer), 0xBEEF);
    CHECK_EQ(util::readU32(buffer + 2), 0x12345678u);
}
