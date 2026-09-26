"""Show Wi-Fi analysis results on the FreeWili 2 built-in panel via OneWili CM0.

Builds a custom panel with text controls once, then updates only the captions
that changed each cycle. The red "Stop" button and keypad X end the app.

Launched from Linux > Apps there is no terminal, so a crash looks like a frozen
screen. Mailbox requests can occasionally go unanswered (for example while MAIN
is busy with other USB traffic), so this class never lets one failed request end
the app: a failed caption is repainted on the next cycle, a failed button poll
counts as "not pressed", and repeated failures reconnect and rebuild the panel.
Only idempotent display text and the button latch are retried; no hardware
actuation is ever replayed.
"""
from __future__ import annotations

# Panel palette (0xRRGGBB), matching the muted dark theme used by BSP examples.
BG, FG, ACCENT, MUTED = 0x101820, 0xE8EEF2, 0x62E69A, 0xA9B7C6

# Control indices for the fields we repaint each cycle.
BAND24 = 3
BAND5 = 4
RECOMMEND = 5
STRONGEST = 6
DETAIL = 7
STATUS = 8

# read_buttons() bits: 0-4 gray/yellow/green/blue/red, keypad X/Cancel = 11.
STOP_MASK = (1 << 4) | (1 << 11)
RECONNECT_AFTER = 3  # consecutive failed requests before rebuilding the panel


def panel_text(value, limit: int = 31) -> str:
    """Fit the firmware's 31-byte ASCII caption, escaping its marker bytes."""
    result = ""
    for char in str(value if value is not None else ""):
        char = char if " " <= char <= "~" else "?"
        encoded = char * 2 if char in "#`" else char  # '#' and '`' are markers
        if len(result) + len(encoded) > limit:
            break
        result += encoded
    return result


class WifiPanel:
    """A live analysis panel on the FreeWili 2 display that survives mailbox errors."""

    def __init__(self, timeout: int = 8, connect=None, log=print):
        self._timeout = timeout
        self._connect = connect
        self._log = log
        self._dev = None
        self._previous: dict[int, str] = {}
        self._wanted: dict[int, str] = {}
        self.failures = 0
        self._reported = False

    @property
    def is_open(self) -> bool:
        return self._dev is not None

    # -- connection ----------------------------------------------------------
    def open(self) -> "WifiPanel":
        connect = self._connect
        if connect is None:
            from onewili_cm0 import connect_cm0 as connect
        self._dev = connect(timeout=self._timeout)
        try:
            gui = self._dev.gui
            gui.panels.show_panel(0).unwrap()
            gui.panels.add_panel(False, 0, BG, True).unwrap()
            gui.panels.set_menu_text(4, "Stop").unwrap()
            rows = [
                (0, 20, 14, 0, ACCENT, "WI-FI ANALYZER"),
                (1, 20, 49, 0, MUTED, "CM0  |  USB Wi-Fi"),
                (2, 20, 95, 3, FG, "((o))"),
                (BAND24, 240, 88, 0, FG, "2.4G --  ch --"),
                (BAND5, 240, 113, 0, FG, "5G   --  ch --"),
                (RECOMMEND, 240, 138, 0, ACCENT, "USE CH --"),
                (STRONGEST, 20, 190, 0, FG, "Scanning..."),
                (DETAIL, 20, 214, 0, MUTED, ""),
                (STATUS, 20, 247, 0, MUTED, "starting"),
                (9, 20, 270, 0, MUTED, "Red / X: stop"),
            ]
            for index, x, y, size, color, caption in rows:
                gui.controls.add_text(index, x, y, 0, size, color, BG, panel_text(caption)).unwrap()
                self._previous[index] = panel_text(caption)
        except BaseException:
            self.close()
            raise
        self.failures = 0
        if self._reported:
            self._log("wifi_analyzer: display reconnected")
            self._reported = False
        return self

    def ensure_open(self) -> bool:
        """(Re)build the panel if it is closed or has failed repeatedly."""
        if self._dev is not None and self.failures < RECONNECT_AFTER:
            return True
        self.close()
        try:
            self.open()
        except Exception as error:
            self._failed(error)
            return False
        for index, value in list(self._wanted.items()):
            self._set(index, value)  # restore the latest captions after a rebuild
        return self._dev is not None

    def close(self) -> None:
        dev, self._dev = self._dev, None
        self._previous = {}
        if dev is not None:
            try:
                dev.close()
            except Exception:
                pass

    def _failed(self, error: Exception) -> None:
        self.failures += 1
        if not self._reported:  # one log line per outage, not one per request
            self._log(f"wifi_analyzer: display request failed ({error}); will retry")
            self._reported = True

    # -- drawing -------------------------------------------------------------
    def _set(self, index: int, value: str) -> None:
        value = panel_text(value)
        self._wanted[index] = value
        if self._dev is None or self._previous.get(index) == value:
            return
        try:
            self._dev.gui.control_properties.set_control_value_text(index, value).unwrap()
        except Exception as error:
            self._failed(error)  # not cached, so the next cycle repaints it
            return
        self._previous[index] = value
        self.failures = 0

    def update(self, result: dict, status: str = "live") -> None:
        b24 = result["bands"].get("2.4", {})
        b5 = result["bands"].get("5", {})
        self._set(BAND24, f"2.4G {b24.get('ap_count', 0):>2}  ch {b24.get('busiest_channel', 0) or '-'}")
        self._set(BAND5, f"5G   {b5.get('ap_count', 0):>2}  ch {b5.get('busiest_channel', 0) or '-'}")
        self._set(RECOMMEND, f"USE CH {result.get('recommended_24_channel', '-')}")
        strongest = b5.get("strongest") or b24.get("strongest")
        if strongest:
            self._set(STRONGEST, strongest["ssid"])
            gen = f" {strongest['generation']}" if strongest.get("generation") else ""
            self._set(DETAIL, f"{strongest['signal_dbm']} dBm ch{strongest['channel']}{gen}")
        else:
            self._set(STRONGEST, "No networks seen")
            self._set(DETAIL, "")
        self._set(STATUS, f"{result.get('total_aps', 0)} APs | {status}")

    def show_message(self, title: str, detail: str = "") -> None:
        """Surface a status or error on the panel (there is no terminal on-device)."""
        self._set(STRONGEST, title)
        self._set(DETAIL, detail)
        self._set(STATUS, "waiting")

    def mark_stopped(self) -> None:
        self._set(STATUS, "stopped")

    def should_stop(self) -> bool:
        """True when Stop (red) or keypad X was pressed; a failed poll means no."""
        if self._dev is None:
            return False
        try:
            pressed = self._dev.gui.panels.read_buttons().unwrap()
        except Exception as error:
            self._failed(error)
            return False
        self.failures = 0
        return bool(pressed & STOP_MASK)

    def __enter__(self) -> "WifiPanel":
        self.ensure_open()
        return self

    def __exit__(self, *exc) -> None:
        self.close()
