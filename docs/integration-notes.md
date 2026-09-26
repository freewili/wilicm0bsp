# CM0 integration and recovery notes

These notes come from a FreeWili 2 CM0 application integration tested on
2026-09-26 with ARM64 Debian 13, kernel 6.12.75+rpt-rpi-v8, and a TP-Link
Archer T3U USB adapter (2357:012d, RTL8812BU, `rtw88_8822bu`). They describe
observations on one device, not a qualification of every image or adapter.
See [driver setup](driver.md) before changing the installed bridge.

## Keep firmware and transport clocks together

Record MAIN firmware and FPGA artifact hashes alongside the Linux image,
UART source clock, UART baud, and both sides' SPI settings. A baud override
alone cannot repair an incompatible FPGA image or UART source clock.

| Setting | BSP reference configuration | Project-specific recovery build |
| --- | --- | --- |
| Linux UART source (`init_uart_clock`) | 125,000,000 Hz | 80,000,000 Hz |
| CM0 UART (`FWCM0_BAUD`) | 7,812,500 baud | 5,000,000 baud |
| CM0 SPI (`FWCM0_SPI_HZ`) | 7,000,000 Hz | 4,000,000 Hz |

The recovery build also used a 20 MHz FPGA clock and 2 MHz MAIN register SPI.
It required matching custom MAIN/gateware; it is **not an alternative preset
for shipping firmware**. Its FPGA implementation reached 22.96 MHz in static
timing analysis, below the original 31.25 MHz target. Do not infer timing
closure from successful synthesis or a few successful requests.

Back up the paired settings and firmware before maintenance. Restore the
matching set when rolling back. USB-mode tools should edit only their owned
settings; restoring an old whole `config.txt` can silently undo a later clock
update. The bridge service explicitly reads `/etc/environment`; confirm its
effective overrides as well as the boot configuration.

A gateware response-sequencing defect found during this integration required
waiting for the encoder's `frame_done`, including CRC/COBS output, before
starting the next response. The regression failed on the original logic and
passed after the fix with UART backpressure. That fix belongs in the FPGA
source repository; the BSP cannot fix it by retrying menu commands. Record the
actual compatible firmware revision rather than assuming all current firmware
contains that change.

## USB host mode and adapter recovery

On the tested FreeWili 2 wiring, host mode used
`dtoverlay=dwc2,dr_mode=host`, GPIO2 high and GPIO3 high. The gadget configuration
used peripheral mode and GPIO2 low with GPIO3 still high. The host boot command
line loaded `dwc2` without the gadget `g_serial` module. These are maintenance
settings to verify against the board/image, not changes for app startup.
Switching modes can remove gadget-serial access; keep an independent access
path and reboot to apply boot configuration changes.

For hotplug-aware applications, track the interface index or equivalent device
identity as well as its name. Replugging can recreate `wlan0` with a different
ifindex and invalidate existing monitor interfaces and capture handles.
A controlled driver unbind/rebind recovered during one test, but did not
recover a later USB fault. Software recovery success is not universal.

The tested adapter intermittently logged USB register-write errors with
`status: -71`, followed by `mac power on failed`. A subsequent monitor-interface
startup reported `Operation already in progress`. Stop the affected app's
restart loop, retain the kernel log, and distinguish a missing USB device from
a device that is enumerated but cannot bring up its radio. A physical adapter
unplug/replug restored operation in the final test; an earlier fault needed a
full device power cycle. The underlying USB fault remains unresolved. These
symptoms do not establish whether power, hardware, or driver behavior caused it.

Useful read-only checks on CM0 (some require installed USB/Wi-Fi utilities):

```sh
lsusb
ip -brief link
iw dev
systemctl show fwcm0-bridge --no-pager -p ActiveState -p SubState -p NRestarts
journalctl -u fwcm0-bridge -n 50 --no-pager
sudo journalctl -k -b -n 100 --no-pager
vcgencmd get_throttled
```

Also inspect the affected application's service and logs. `lsusb` enumeration,
an active service, and `throttled=0x0` each provide partial evidence; none alone
proves a working radio or excludes a peripheral power problem. After recovery,
verify a real operation and check for new errors during a stated observation
period. Do not replay ambiguous hardware writes automatically.

## Boot, capture, and application validation

- Test cold power-up separately from warm reboot. Three warm reboots passed in
  this integration, but a later full power cycle left FPGA/CM0 power disabled
  and CM0 RUN held low until explicitly enabled. A runtime power correction
  does not establish persistent cold-boot behavior. See
  [power ownership](../agents/hardware.md); shut Linux down before cutting power.
- An API accepting a command is not proof of its physical effect. Local capture
  of transmitted packets and virtual-radio tests do not establish independent
  over-the-air reception. Test physical delivery with an appropriate receiver.
- Establish an explicit capture-ready signal before asking a user to trigger
  an event. A reconnect occurring just before readiness produced a partial
  capture here. Record packet timestamps and allow for device/host clock skew.
- For the authorized Wi-Fi lab, a manual phone reconnect produced all four
  pairwise EAPOL messages with matching replay counters in both a separate
  tcpdump capture and bettercap's saved capture on the same adapter. This
  verified physical 2.4 GHz reception and saving. It did not verify an injected
  deauthentication-triggered reconnect, a physical 5 GHz handshake, or mesh
  reception by an independent device. Passive 5 GHz reception was tested
  separately. Keep those claims separate in support reports.
- Restore app-owned temporary interfaces/settings and the normal application
  after a bounded diagnostic, including failure paths; then verify the restart
  actually succeeded. Cleanup completing does not guarantee hardware recovery.
- On the tested module, approximately 416 MiB RAM plus zram required conservative
  resource use. The application's AI startup took roughly two to three minutes.
  Service activation should be distinguished from application readiness, with
  suitable startup timeouts and persistent state kept outside the code folder.

For reproducible reports, retain source revisions, artifact hashes, relevant
settings, test scope, and failure/recovery logs. Remove credentials and
user/device identifiers from material published upstream. These field notes
supplement, rather than replace, the BSP's own [verification record](platform-support.md).
