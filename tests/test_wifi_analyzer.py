"""Cross-platform unit tests for the Wi-Fi analyzer core (no root/hardware).

Run: python3 -m unittest tests/test_wifi_analyzer.py
"""
import sys
import unittest
import unittest.mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "apps" / "wifi_analyzer"))

import analyzer  # noqa: E402

# Representative `iw dev <iface> scan` output: 2.4 GHz (ch 1, 6, hidden on 11)
# and 5 GHz (ch 36 Wi-Fi 6, ch 149 open legacy).
SCAN = """\
BSS 00:11:22:33:44:55(on wlan0)
	freq: 2412
	signal: -42.00 dBm
	SSID: HomeNet
	capability: ESS Privacy ShortSlotTime (0x0411)
	RSN:	 * Version: 1
	HT capabilities:
BSS aa:bb:cc:dd:ee:01(on wlan0)
	freq: 2437
	signal: -68.00 dBm
	SSID: NeighborWifi
	capability: ESS Privacy (0x0411)
	RSN:	 * Version: 1
	HT capabilities:
BSS aa:bb:cc:dd:ee:02(on wlan0)
	freq: 2462
	signal: -75.00 dBm
	SSID:
	capability: ESS Privacy (0x0411)
	RSN:	 * Version: 1
BSS 66:77:88:99:aa:bb(on wlan0)
	freq: 5180
	signal: -51.00 dBm
	SSID: HomeNet-5G
	capability: ESS Privacy (0x0511)
	RSN:	 * Version: 1
	VHT capabilities:
	HE capabilities:
BSS 12:34:56:78:9a:bc(on wlan0)
	freq: 5745
	signal: -80.00 dBm
	SSID: GuestOpen
	capability: ESS (0x0401)
"""


# nmcli -m multiline -f SSID,BSSID,CHAN,FREQ,SIGNAL,SECURITY device wifi list
NMCLI = """\
SSID:      HomeNet
BSSID:     00:11:22:33:44:55
CHAN:      1
FREQ:      2412 MHz
SIGNAL:    92
SECURITY:  WPA2
SSID:
BSSID:     AA:BB:CC:DD:EE:02
CHAN:      6
FREQ:      2437 MHz
SIGNAL:    60
SECURITY:  WPA2
SSID:      GuestOpen
BSSID:     12:34:56:78:9A:BC
CHAN:      149
FREQ:      5745 MHz
SIGNAL:    40
SECURITY:  --
"""

def parsed():
    return analyzer.parse_scan(SCAN)


class WifiAnalyzer(unittest.TestCase):
    def test_freq_mapping(self):
        assert analyzer.freq_to_band(2412) == "2.4"
        assert analyzer.freq_to_band(5180) == "5"
        assert analyzer.freq_to_band(5975) == "6"
        assert analyzer.freq_to_channel(2412) == 1
        assert analyzer.freq_to_channel(2437) == 6
        assert analyzer.freq_to_channel(2462) == 11
        assert analyzer.freq_to_channel(5180) == 36
        assert analyzer.freq_to_channel(5745) == 149
        assert analyzer.freq_to_channel(2484) == 14


    def test_parse_counts_and_fields(self):
        aps = parsed()
        assert len(aps) == 5
        home = next(a for a in aps if a.bssid == "00:11:22:33:44:55")
        assert home.ssid == "HomeNet"
        assert home.band == "2.4" and home.channel == 1
        assert home.signal_dbm == -42.0
        assert home.secured is True
        assert home.generation == "n"


    def test_hidden_and_open_detection(self):
        aps = parsed()
        hidden = next(a for a in aps if a.bssid == "aa:bb:cc:dd:ee:02")
        assert hidden.is_hidden is True
        guest = next(a for a in aps if a.bssid == "12:34:56:78:9a:bc")
        assert guest.secured is False
        assert guest.band == "5" and guest.channel == 149


    def test_generation_detection(self):
        aps = parsed()
        five = next(a for a in aps if a.bssid == "66:77:88:99:aa:bb")
        assert five.generation == "ax"  # HE present -> Wi-Fi 6


    def test_summary_bands(self):
        result = analyzer.analyze(parsed())
        assert result["total_aps"] == 5
        assert result["bands"]["2.4"]["ap_count"] == 3
        assert result["bands"]["2.4"]["hidden"] == 1
        assert result["bands"]["5"]["ap_count"] == 2
        assert result["bands"]["5"]["open_networks"] == 1
        assert result["bands"]["2.4"]["busiest_channel"] in (1, 6, 11)
        assert result["bands"]["5"]["strongest"]["ssid"] == "HomeNet-5G"


    def test_recommend_channel_avoids_congestion(self):
        # APs sit on ch 1 (strong) and ch 6; channel 11 should be least congested.
        best, scores = analyzer.recommend_24_channel(parsed())
        assert best == 11
        assert scores[11] < scores[1]


    def test_merge_keeps_strongest(self):
        weak = analyzer.AccessPoint(bssid="de:ad:be:ef:00:01", band="5", channel=36, signal_dbm=-80.0)
        strong = analyzer.AccessPoint(bssid="de:ad:be:ef:00:01", band="5", channel=36, signal_dbm=-55.0)
        merged = analyzer.merge([[weak], [strong]])
        assert len(merged) == 1
        assert merged[0].signal_dbm == -55.0


    def test_report_is_text(self):
        report = analyzer.format_report(parsed())
        assert "2.4 GHz" in report and "5 GHz" in report
        assert "Least-congested 2.4 GHz channel" in report


    def test_parse_empty_is_safe(self):
        assert analyzer.parse_scan("") == []
        assert analyzer.parse_nmcli("") == []
        result = analyzer.analyze([])
        assert result["total_aps"] == 0




    def test_signal_pct_to_dbm(self):
        assert analyzer.signal_pct_to_dbm(100) == -50.0
        assert analyzer.signal_pct_to_dbm(0) == -100.0
        assert analyzer.signal_pct_to_dbm(60) == -70.0


    def test_parse_nmcli(self):
        aps = analyzer.parse_nmcli(NMCLI)
        assert len(aps) == 3
        home = next(a for a in aps if a.bssid == "00:11:22:33:44:55")
        assert home.ssid == "HomeNet" and home.band == "2.4" and home.channel == 1
        assert home.signal_dbm == -54.0 and home.secured is True
        hidden = next(a for a in aps if a.bssid == "aa:bb:cc:dd:ee:02")
        assert hidden.is_hidden is True
        guest = next(a for a in aps if a.bssid == "12:34:56:78:9a:bc")
        assert guest.band == "5" and guest.channel == 149 and guest.secured is False


    def test_nmcli_analysis_matches_shape(self):
        result = analyzer.analyze(analyzer.parse_nmcli(NMCLI))
        assert result["total_aps"] == 3
        assert result["bands"]["2.4"]["ap_count"] == 2
        assert result["bands"]["2.4"]["hidden"] == 1
        assert result["bands"]["5"]["open_networks"] == 1
        # Text report must not choke on empty generation from nmcli.
        assert "strongest" in analyzer.format_report(analyzer.parse_nmcli(NMCLI))


import setup_usb_host  # noqa: E402

# Layout observed on a FreeWili 2 CM0 image: a host overlay exists, but only
# under [cm5]; the CM0 is configured by the [all] peripheral lines.
BOOT = """\
[cm4]
#otg_mode=1

[cm5]
dtoverlay=dwc2,dr_mode=host

[all]
enable_uart=1
gpio=16,17=a3

[all]
dtoverlay=dwc2,dr_mode=peripheral
gpio=2=op,dl
gpio=3=op,dh
init_uart_clock=125000000
"""


class UsbHostSetup(unittest.TestCase):
    def test_host_edits_only_all_section_usb_lines(self):
        host = setup_usb_host.host_config(BOOT)
        self.assertIn("[all]\ndtoverlay=dwc2,dr_mode=host\ngpio=2=op,dh\ngpio=3=op,dh\n", host)
        self.assertIn("[cm5]\ndtoverlay=dwc2,dr_mode=host\n", host)
        self.assertEqual(len(host.splitlines()), len(BOOT.splitlines()))
        self.assertIn("init_uart_clock=125000000", host)

    def test_restore_round_trips_and_keeps_cm5(self):
        host = setup_usb_host.host_config(BOOT)
        self.assertEqual(setup_usb_host.gadget_config(host), BOOT)
        self.assertEqual(setup_usb_host.host_config(host), host)       # idempotent
        self.assertEqual(setup_usb_host.gadget_config(BOOT), BOOT)     # already gadget

    def test_cm5_host_line_does_not_fake_host_mode(self):
        broken = BOOT.replace("[all]\ndtoverlay=dwc2,dr_mode=peripheral\n", "[all]\n")
        with self.assertRaises(ValueError):
            setup_usb_host.host_config(broken)

    def test_cmdline(self):
        line = "console=tty1 root=PARTUUID=x rootwait modules-load=dwc2,g_serial\n"
        host = setup_usb_host.usb_cmdline(line)
        self.assertIn("modules-load=dwc2\n", host)
        self.assertEqual(setup_usb_host.usb_cmdline(host, gadget=True), line)


import display  # noqa: E402
from types import SimpleNamespace  # noqa: E402


class Res:
    """Minimal stand-in for a OneWili Result."""

    def __init__(self, value=None, error=None):
        self.value, self.error = value, error

    def unwrap(self):
        if self.error:
            raise RuntimeError(self.error)
        return self.value


class FakeDevice:
    def __init__(self, fail_buttons=0, fail_text=0):
        self.captions, self.closed, self.buttons = {}, False, 0
        self.fail_buttons, self.fail_text = fail_buttons, fail_text
        ok = lambda *args: Res()  # noqa: E731
        self.gui = SimpleNamespace(
            panels=SimpleNamespace(show_panel=ok, add_panel=ok, set_menu_text=ok,
                                   read_buttons=self.read_buttons),
            controls=SimpleNamespace(add_text=self.add_text),
            control_properties=SimpleNamespace(set_control_value_text=self.set_text))

    def add_text(self, index, *args):
        self.captions[index] = args[-1]
        return Res()

    def set_text(self, index, text):
        if self.fail_text:
            self.fail_text -= 1
            return Res(error="timeout")
        self.captions[index] = text
        return Res()

    def read_buttons(self):
        if self.fail_buttons:
            self.fail_buttons -= 1
            return Res(error="timeout waiting for response to 'g\\c\\e'")
        pressed, self.buttons = self.buttons, 0
        return Res(pressed)

    def close(self):
        self.closed = True


import app  # noqa: E402


class NmcliPreconditions(unittest.TestCase):
    def fake_run(self, radio, status):
        def run(*args, check=True, timeout=25):
            text = radio if args[:3] == ("nmcli", "-t", "radio") else status
            return SimpleNamespace(returncode=0, stdout=text)
        return run

    def test_disabled_radio_is_reported_not_shown_as_zero_networks(self):
        with unittest.mock.patch.object(app, "run", self.fake_run("disabled\n", "wlan0:unavailable\n")):
            with self.assertRaisesRegex(RuntimeError, "radio is off"):
                app.scan_nmcli("wlan0")
        title, hint = app._panel_message(RuntimeError("Wi-Fi radio is off (nmcli radio wifi on)"))
        self.assertEqual((title, hint), ("Wi-Fi radio is off", "nmcli radio wifi on"))

    def test_unavailable_device_is_reported(self):
        with unittest.mock.patch.object(app, "run", self.fake_run("enabled\n", "wlan0:unavailable\n")):
            with self.assertRaisesRegex(RuntimeError, "unavailable"):
                app.scan_nmcli("wlan0")


class PanelResilience(unittest.TestCase):
    def panel(self, *devices):
        queue = list(devices)
        logs = []

        def connect(timeout):
            item = queue.pop(0)
            if isinstance(item, Exception):
                raise item
            return item
        return display.WifiPanel(connect=connect, log=logs.append), logs

    def test_button_poll_timeout_does_not_end_the_app(self):
        # Reproduces the device failure: first read_buttons() had no reply.
        dev = FakeDevice(fail_buttons=1)
        panel, logs = self.panel(dev)
        self.assertTrue(panel.ensure_open())
        self.assertFalse(panel.should_stop())
        dev.buttons = 1 << 4  # red
        self.assertTrue(panel.should_stop())
        self.assertEqual(len(logs), 1)

    def test_failed_caption_is_repainted_next_cycle(self):
        dev = FakeDevice(fail_text=1)
        panel, _ = self.panel(dev)
        panel.ensure_open()
        panel.show_message("No Wi-Fi adapter", "Plug USB Wi-Fi into CM0 host")
        self.assertNotEqual(dev.captions[display.STRONGEST], "No Wi-Fi adapter")
        panel.show_message("No Wi-Fi adapter", "Plug USB Wi-Fi into CM0 host")
        self.assertEqual(dev.captions[display.STRONGEST], "No Wi-Fi adapter")

    def test_repeated_failures_rebuild_panel_with_latest_captions(self):
        first, second = FakeDevice(fail_text=99), FakeDevice()
        panel, logs = self.panel(first, second)
        panel.ensure_open()
        for _ in range(display.RECONNECT_AFTER):
            panel.show_message("No Wi-Fi adapter", "hint")
        self.assertTrue(panel.ensure_open())
        self.assertTrue(first.closed)
        self.assertEqual(second.captions[display.STRONGEST], "No Wi-Fi adapter")
        self.assertEqual(second.captions[display.STATUS], "waiting")
        self.assertIn("reconnected", logs[-1])

    def test_busy_session_at_launch_retries_instead_of_exiting(self):
        dev = FakeDevice()
        panel, _ = self.panel(RuntimeError("busy"), dev)
        self.assertFalse(panel.ensure_open())
        self.assertFalse(panel.should_stop())
        panel.show_message("x", "y")          # no device yet: remembered, not raised
        self.assertTrue(panel.ensure_open())
        self.assertEqual(dev.captions[display.STRONGEST], "x")

    def test_captions_fit_the_firmware_limit(self):
        self.assertEqual(len(display.panel_text("x" * 50)), 31)
        self.assertEqual(display.panel_text("ch#1"), "ch##1")
        self.assertLessEqual(len("Plug USB Wi-Fi into CM0 host"), 31)


if __name__ == "__main__":
    unittest.main()
