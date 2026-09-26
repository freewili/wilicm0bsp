#include <gtest/gtest.h>
#include "fwcm0/cobs.h"
#include <vector>
using namespace fwcm0; using B = std::vector<uint8_t>;
TEST(Cobs, KnownVectors) {
  EXPECT_EQ(cobs_encode({}),                 (B{0x01}));
  EXPECT_EQ(cobs_encode({0x00}),             (B{0x01,0x01}));
  EXPECT_EQ(cobs_encode({0x11,0x22,0x00,0x33}), (B{0x03,0x11,0x22,0x02,0x33}));
  EXPECT_EQ(cobs_decode({0x03,0x11,0x22,0x02,0x33}), (B{0x11,0x22,0x00,0x33}));
}
TEST(Cobs, RoundTripIncludingLongRuns) {
  for (size_t n : {0u,1u,253u,254u,255u,300u,600u}) {
    B in(n); for (size_t i=0;i<n;++i) in[i]=static_cast<uint8_t>((i*7+1)&0xFF); // no 0x00
    EXPECT_EQ(cobs_decode(cobs_encode(in)), in) << "n="<<n;
    B inz = in; if(n>2){inz[1]=0x00; inz[n-1]=0x00;}
    EXPECT_EQ(cobs_decode(cobs_encode(inz)), inz) << "with zeros n="<<n;
  }
}
