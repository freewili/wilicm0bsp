# Hardware and transport boundaries

CM0 is FreeWili 2's BCM2837/CM3-class Linux processor. MAIN owns the firmware
menu API and many external peripherals; DISPLAY owns the screen and its own
peripherals. Use OneWili to ask the firmware to control those peripherals.
The module name CM0 does not mean Cortex-M0 or Raspberry Pi CM4.

The Linux driver writes FPGA commands over SPI0 and receives replies on the
PL011 UART with RTS/CTS. Current defaults: `/dev/spidev0.0`, `/dev/ttyAMA0`,
GPIO21 software chip select, 7 MHz SPI, 7,812,500 baud UART. Firmware and
gateware must match these values. `config/config-fwcm0.txt` is a reference
for image maintainers; app installation does not change boot settings.

`fwcm0-bridge` owns that link. Python uses `fwcm0 api`; BSP C++ uses its Unix
socket directly. Both select the isolated OneWili API channel (protocol 1.2+).
The Linux shell uses another mailbox channel, so it can stay open.

The low-level `fwcm0::LinuxTransport`, router examples and OneWili's direct
`cm0` C++ adapter own hardware themselves: stop the bridge only for deliberate
driver maintenance and restart it afterward. Prefer `wilicm0::Device` for apps.

FPGA is power zone 6; CM0 is zone 17; CM0 RUN is control line 19. Do not cut
CM0 power while Linux is writing. Shut Linux down first. From a PC,
`tools/deploy.py cm0 status|reboot|usb` wraps these MAIN controls; its
`cm0 reset --force` (RUN line) is only for an unreachable CM0 and may be
refused by MAIN. See [PC deployment](../docs/deploy.md). Other peripheral power
requirements are enforced by MAIN and reported as `EPOWERZONE` failures.
