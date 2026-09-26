#include <gtest/gtest.h>
#include "fwcm0/protocol.h"
using namespace fwcm0;

TEST(Protocol, Opcodes) {
  EXPECT_EQ(OP_WRITE_AT, 0x02); EXPECT_EQ(OP_WRITE_CONT, 0x0A);
  EXPECT_EQ(OP_READ_AT, 0x03);  EXPECT_EQ(OP_READ_CONT, 0x0B);
  EXPECT_EQ(OP_STATUS, 0x05);
}
TEST(Protocol, Frames) {
  EXPECT_EQ(FT_READ_RESP, 0x01); EXPECT_EQ(FT_STATUS, 0x02);
  EXPECT_EQ(FT_NOTIFY, 0x03);    EXPECT_EQ(FT_NAK, 0x04);
  EXPECT_EQ(NS_BOOT_READY, 0x01); EXPECT_EQ(NS_SWAP_READY, 0x02);
}
TEST(Protocol, Flags) {
  EXPECT_EQ(FLAG_INIT_DONE, 0x80); EXPECT_EQ(FLAG_BOOT_READY, 0x40);
  EXPECT_EQ(FLAG_WR_ACTIVE, 0x08); EXPECT_EQ(FLAG_RD_ACTIVE, 0x04);
  EXPECT_EQ(FLAG_ABORT, 0x02);
}
TEST(Protocol, ShellConstantsMatchMainContract) {
  // Mirror of freewilimain/rmpLib/rpCM0Comm.h CM0_TYPE_SHELL / CM0_CTRL_SHELL_ATTACH.
  EXPECT_EQ(fwcm0::MBOX_TYPE_SHELL, 0x02);
  EXPECT_EQ(fwcm0::MBOX_CTRL_SHELL_ATTACH, 0x04);
  // Distinct from the console/control constants they coexist with.
  EXPECT_NE(fwcm0::MBOX_TYPE_SHELL, fwcm0::MBOX_TYPE_CONTROL);
  EXPECT_NE(fwcm0::MBOX_CTRL_SHELL_ATTACH, fwcm0::MBOX_CTRL_RESET);
}
