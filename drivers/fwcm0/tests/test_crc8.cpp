#include <gtest/gtest.h>
#include "fwcm0/crc8.h"
using namespace fwcm0;
TEST(Crc8, KnownVectors) {
  EXPECT_EQ(crc8(nullptr, 0), 0x00);            // empty -> init 0x00
  uint8_t one = 0x01;
  EXPECT_EQ(crc8(&one, 1), 0x07);               // poly 0x07, MSB-first
  uint8_t z = 0x00;
  EXPECT_EQ(crc8(&z, 1), 0x00);
}
// NOTE: during HW integration, extend with vectors captured from the FPGA's
// cm0_frame_test_pkg::crc8_calc to confirm byte-identical CRC with the gateway.
