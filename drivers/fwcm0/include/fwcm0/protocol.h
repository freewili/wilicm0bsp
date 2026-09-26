#pragma once
#include <cstdint>
// Mirror of freewilifpga/fpga/src/dsn/cm0_router_defs.vh - keep in sync.
namespace fwcm0 {
constexpr uint8_t OP_WRITE_AT = 0x02, OP_WRITE_CONT = 0x0A;
constexpr uint8_t OP_READ_AT  = 0x03, OP_READ_CONT  = 0x0B;
constexpr uint8_t OP_STATUS   = 0x05;
constexpr uint8_t OP_SWAP_REQUEST = 0x06;            // fire-and-forget, no response
constexpr uint8_t FT_READ_RESP = 0x01, FT_STATUS = 0x02, FT_NOTIFY = 0x03, FT_NAK = 0x04;
constexpr uint8_t NS_BOOT_READY = 0x01, NS_SWAP_READY = 0x02;
// Mailbox console bridge (CM0 <-> MAIN). The FPGA moves [type][payload]
// opaquely; the body constants below are interpreted by the CM0 console client
// and MAIN rpCM0Comm, not the gateware. Keep OP_MBOX/FT_MBOX synced with the .vh.
constexpr uint8_t OP_MBOX = 0x07;            // CM0->FPGA mailbox message (SPI0)
constexpr uint8_t FT_MBOX = 0x05;            // FPGA->CM0 mailbox frame (UART)
constexpr uint8_t MBOX_TYPE_CONSOLE = 0x00, MBOX_TYPE_CONTROL = 0x01;
constexpr uint8_t MBOX_CTRL_SET_STREAM = 0x01, MBOX_CTRL_HELLO = 0x02, MBOX_CTRL_RESET = 0x03;
constexpr uint8_t MBOX_TYPE_SHELL = 0x02;         // raw bash byte stream, MAIN <-> CM0
constexpr uint8_t MBOX_CTRL_SHELL_ATTACH = 0x04;  // MAIN -> CM0: [enable] spawn/kill bash
constexpr uint8_t MBOX_CTRL_SHELL_EXIT   = 0x05;  // CM0 -> MAIN: bash ended on its own, drop the attach
constexpr uint8_t CRC8_POLY = 0x07;
constexpr uint8_t NAK_ABORTED_READ = 0xFF;   // NAK reason for a read aborted by a swap
constexpr uint8_t FLAG_INIT_DONE = 1u<<7, FLAG_BOOT_READY = 1u<<6, FLAG_QUIESCED = 1u<<5,
                  FLAG_READY = 1u<<4, FLAG_WR_ACTIVE = 1u<<3, FLAG_RD_ACTIVE = 1u<<2,
                  FLAG_ABORT = 1u<<1;
}
