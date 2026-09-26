# Application instructions

Read [the BSP agent guide](../../AGENTS.md) before editing.
Install in `/home/apps/wifi_analyzer/` for Linux Apps launching.

Launched from Linux > Apps with no arguments, it drives the built-in screen
through the OneWili CM0 API and runs as the console user (no root). Keep it that
way: scanning uses `nmcli` (NetworkManager scans for us, non-root) and falls
back to `iw` only when root. Never add a root requirement to the default path.

This app is **passive and analysis-only** by design. Keep it that way:

- No transmit, association, deauthentication, monitor mode, or packet
  injection/capture. Surveying is receive-only (`nmcli`/`iw` scan) exclusively.
- Do not stop the `fwcm0` bridge, reset the FPGA, or change boot configuration
  as part of running. `setup_usb_host.py` changes boot config and is a separate,
  manual, one-time step — never call it from `app.py`.
- Leave interface state alone (the nmcli path never touches it). Check every
  OneWili `Result`; in the display, a failed request is retried on the next
  cycle or triggers a reconnect rather than ending the app. Close the connection.

Keep `analyzer.py` free of I/O so it stays importable and testable on Windows,
macOS, and Linux. Add cases to `tests/test_wifi_analyzer.py` for parsing/analysis
changes and run `python3 -m unittest tests/test_wifi_analyzer.py`. Live scanning and
display are Linux/CM0-only; record real device checks in the BSP's
`docs/platform-support.md`.
