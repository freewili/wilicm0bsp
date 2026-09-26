#include <gtest/gtest.h>
#include "fwcm0/mock_transport.h"
using namespace fwcm0;

TEST(Mock, CapturesCommandAndServesFrame){
  MockTransport t;
  uint8_t cmd[] = {OP_STATUS, 0x44};
  t.begin_command(); t.spi_write(cmd, 2); t.end_command();
  ASSERT_EQ(t.commands.size(), 1u);
  EXPECT_EQ(t.commands[0], (std::vector<uint8_t>{OP_STATUS,0x44}));
  t.inject_frame(FT_NAK, 0x44, {0xFF});
  uint8_t buf[64]; size_t n = t.uart_read(buf, 64, 100);
  EXPECT_GT(n, 0u); EXPECT_EQ(buf[n-1], 0x00);  // ends with delimiter
}
