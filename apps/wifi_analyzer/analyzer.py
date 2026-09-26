"""Passive 2.4/5 GHz Wi-Fi analysis: parse `iw dev <iface> scan` output.

Pure functions with no I/O, root, or hardware dependency so they run and test on
any platform. `app.py` supplies the scan text captured on the CM0; everything
here only reads it. This is passive observation of beacons and probe responses
that any station receives; it neither transmits nor associates.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Iterable

# Non-overlapping 2.4 GHz channels used for the "least congested" recommendation.
NON_OVERLAPPING_24 = (1, 6, 11)


@dataclass
class AccessPoint:
    bssid: str
    ssid: str = ""
    freq_mhz: int = 0
    channel: int = 0
    band: str = ""          # "2.4", "5", "6", or "" if unknown
    signal_dbm: float | None = None
    secured: bool = True     # False only when no privacy/RSN/WPA is advertised
    generation: str = ""     # "ax", "ac", "n", or "legacy"

    @property
    def is_hidden(self) -> bool:
        return self.ssid == ""


def freq_to_band(freq_mhz: int) -> str:
    """Map a center frequency in MHz to a Wi-Fi band label."""
    if 2400 <= freq_mhz <= 2500:
        return "2.4"
    if 5150 <= freq_mhz <= 5925:
        return "5"
    if 5925 < freq_mhz <= 7125:
        return "6"
    return ""


def freq_to_channel(freq_mhz: int) -> int:
    """Map a center frequency in MHz to its channel number (0 if unknown)."""
    if freq_mhz == 2484:
        return 14
    if 2412 <= freq_mhz <= 2472:
        return (freq_mhz - 2407) // 5
    if 5150 <= freq_mhz <= 5925:
        return (freq_mhz - 5000) // 5
    if 5925 < freq_mhz <= 7125:
        return (freq_mhz - 5950) // 5
    return 0


def _generation(block: str) -> str:
    if "HE capabilities" in block or "HE Capabilities" in block:
        return "ax"
    if "VHT capabilities" in block or "VHT Capabilities" in block:
        return "ac"
    if "HT capabilities" in block or "HT Capabilities" in block:
        return "n"
    return "legacy"


def _secured(block: str) -> bool:
    if "RSN:" in block or "WPA:" in block:
        return True
    # Fall back to the Privacy bit in the capability field for WEP-era networks.
    match = re.search(r"^\s*capability:\s*(.*)$", block, re.MULTILINE)
    return bool(match and "Privacy" in match.group(1))


def parse_scan(text: str) -> list[AccessPoint]:
    """Parse `iw dev <iface> scan` output into a list of AccessPoint records.

    Unknown or malformed blocks are skipped rather than raising, so a partial
    capture still yields whatever the tool managed to report.
    """
    results: list[AccessPoint] = []
    # Each network starts with a "BSS <mac>" line; split on it, keep the mac.
    blocks = re.split(r"(?m)^BSS\s+([0-9a-fA-F:]{17})", text)
    # re.split with one capture group yields: [pre, mac1, body1, mac2, body2, ...]
    for i in range(1, len(blocks), 2):
        bssid = blocks[i].lower()
        block = blocks[i + 1] if i + 1 < len(blocks) else ""

        freq_match = re.search(r"^\s*freq:\s*([0-9]+)", block, re.MULTILINE)
        freq = int(freq_match.group(1)) if freq_match else 0

        sig_match = re.search(r"^\s*signal:\s*(-?[0-9.]+)\s*dBm", block, re.MULTILINE)
        signal = float(sig_match.group(1)) if sig_match else None

        # Only spaces/tabs after the colon: \s* would eat the newline of an empty
        # (hidden) SSID and grab the following line.
        ssid_match = re.search(r"^[ \t]*SSID:[ \t]*(.*)$", block, re.MULTILINE)
        ssid = ssid_match.group(1).strip() if ssid_match else ""
        # iw prints an empty SSID for hidden networks; keep it as "".

        # Prefer the DS/HT-advertised channel, else derive from frequency.
        ch_match = re.search(r"(?:DS Parameter set|primary channel):\s*(?:channel\s*)?([0-9]+)",
                             block)
        channel = int(ch_match.group(1)) if ch_match else freq_to_channel(freq)

        results.append(AccessPoint(
            bssid=bssid,
            ssid=ssid,
            freq_mhz=freq,
            channel=channel,
            band=freq_to_band(freq),
            signal_dbm=signal,
            secured=_secured(block),
            generation=_generation(block),
        ))
    return results


def signal_pct_to_dbm(percent: float) -> float:
    """Approximate NetworkManager's 0-100 signal quality as dBm.

    NM reports quality, not power; this is the common linear mapping
    (0% -> -100 dBm, 100% -> -50 dBm) so the analysis can treat both
    backends uniformly.
    """
    return round(percent / 2.0 - 100.0, 1)


def parse_nmcli(text: str) -> list[AccessPoint]:
    """Parse `nmcli -m multiline -f SSID,BSSID,CHAN,FREQ,SIGNAL,SECURITY ...`.

    Multiline output is one "FIELD: value" per line, a new record beginning at
    each SSID line (so BSSIDs, which contain colons, parse unambiguously). This
    path needs no root: NetworkManager performs the scan on our behalf.
    """
    results: list[AccessPoint] = []
    current: dict[str, str] | None = None

    def flush(rec: dict[str, str] | None) -> None:
        if not rec or "BSSID" not in rec:
            return
        freq = 0
        freq_match = re.search(r"(\d+)", rec.get("FREQ", ""))
        if freq_match:
            freq = int(freq_match.group(1))
        channel = int(rec["CHAN"]) if rec.get("CHAN", "").isdigit() else freq_to_channel(freq)
        signal = None
        if rec.get("SIGNAL", "").lstrip("-").isdigit():
            signal = signal_pct_to_dbm(float(rec["SIGNAL"]))
        security = rec.get("SECURITY", "").strip()
        results.append(AccessPoint(
            bssid=rec["BSSID"].strip().lower(),
            ssid=rec.get("SSID", "").strip(),
            freq_mhz=freq,
            channel=channel,
            band=freq_to_band(freq),
            signal_dbm=signal,
            secured=bool(security and security not in ("--", "none", "NONE")),
            generation="",  # nmcli does not expose the PHY generation
        ))

    for line in text.splitlines():
        match = re.match(r"^(\S[^:]*):\s?(.*)$", line)
        if not match:
            continue
        field, value = match.group(1).strip(), match.group(2).strip()
        if field == "SSID":
            flush(current)
            current = {}
        if current is not None:
            current[field] = value
    flush(current)
    return results


def merge(scans: Iterable[list[AccessPoint]]) -> list[AccessPoint]:
    """Merge several scan passes, keeping the strongest sighting per BSSID."""
    best: dict[str, AccessPoint] = {}
    for scan in scans:
        for ap in scan:
            existing = best.get(ap.bssid)
            if existing is None:
                best[ap.bssid] = ap
                continue
            new_sig = ap.signal_dbm if ap.signal_dbm is not None else -999.0
            old_sig = existing.signal_dbm if existing.signal_dbm is not None else -999.0
            if new_sig > old_sig:
                best[ap.bssid] = ap
    return list(best.values())


def _linear_weight(signal_dbm: float | None) -> float:
    """Relative airtime-impact weight of an AP from its RSSI (dimensionless)."""
    if signal_dbm is None:
        return 0.1
    # Convert dBm to a bounded linear proxy so a -40 dBm AP counts far more than
    # a -90 dBm one, without letting a single strong AP dominate unboundedly.
    return 10.0 ** (max(-95.0, min(-30.0, signal_dbm)) / 10.0) * 1e9


def recommend_24_channel(aps: list[AccessPoint]) -> tuple[int, dict[int, float]]:
    """Recommend the least-congested 2.4 GHz channel among 1/6/11.

    Congestion for a candidate is the signal-weighted sum of nearby APs, where a
    20 MHz channel overlaps candidates within 4 channels (linear falloff).
    Returns (best_channel, {candidate: score}).
    """
    scores: dict[int, float] = {c: 0.0 for c in NON_OVERLAPPING_24}
    for ap in aps:
        if ap.band != "2.4" or not ap.channel:
            continue
        for candidate in NON_OVERLAPPING_24:
            delta = abs(candidate - ap.channel)
            overlap = max(0.0, (5 - delta) / 5.0)  # 1.0 same channel, 0 at >=5 apart
            if overlap:
                scores[candidate] += overlap * _linear_weight(ap.signal_dbm)
    best = min(scores, key=scores.get)
    return best, scores


@dataclass
class BandSummary:
    band: str
    ap_count: int = 0
    hidden: int = 0
    open_networks: int = 0
    channels: dict[int, int] = field(default_factory=dict)   # channel -> AP count
    strongest: AccessPoint | None = None

    @property
    def busiest_channel(self) -> int:
        return max(self.channels, key=self.channels.get) if self.channels else 0


def summarize(aps: list[AccessPoint]) -> dict[str, BandSummary]:
    """Group APs by band and compute per-band counts, channels, and extremes."""
    summaries: dict[str, BandSummary] = {
        "2.4": BandSummary("2.4"), "5": BandSummary("5"), "6": BandSummary("6"),
    }
    for ap in aps:
        summary = summaries.get(ap.band)
        if summary is None:
            continue
        summary.ap_count += 1
        if ap.is_hidden:
            summary.hidden += 1
        if not ap.secured:
            summary.open_networks += 1
        if ap.channel:
            summary.channels[ap.channel] = summary.channels.get(ap.channel, 0) + 1
        sig = ap.signal_dbm if ap.signal_dbm is not None else -999.0
        cur = summary.strongest.signal_dbm if summary.strongest and summary.strongest.signal_dbm is not None else -1000.0
        if sig > cur:
            summary.strongest = ap
    return summaries


def analyze(aps: list[AccessPoint]) -> dict:
    """Produce the full analysis result as a plain dict (JSON-serializable)."""
    summaries = summarize(aps)
    rec_channel, rec_scores = recommend_24_channel(aps)
    return {
        "total_aps": len(aps),
        "bands": {
            band: {
                "ap_count": s.ap_count,
                "hidden": s.hidden,
                "open_networks": s.open_networks,
                "busiest_channel": s.busiest_channel,
                "channels": dict(sorted(s.channels.items())),
                "strongest": _ap_dict(s.strongest),
            }
            for band, s in summaries.items()
            if s.ap_count or band in ("2.4", "5")
        },
        "recommended_24_channel": rec_channel,
        "channel_24_congestion": {str(k): round(v, 3) for k, v in rec_scores.items()},
    }


def _ap_dict(ap: AccessPoint | None) -> dict | None:
    if ap is None:
        return None
    return {
        "bssid": ap.bssid,
        "ssid": ap.ssid or "<hidden>",
        "channel": ap.channel,
        "freq_mhz": ap.freq_mhz,
        "signal_dbm": ap.signal_dbm,
        "generation": ap.generation,
        "secured": ap.secured,
    }


def _bar(count: int, scale: int = 1) -> str:
    return "#" * min(40, count * scale)


def format_report(aps: list[AccessPoint], result: dict | None = None) -> str:
    """Render a human-readable text report for the Linux Apps log / console."""
    result = result or analyze(aps)
    lines = ["=== FreeWili 2 Wi-Fi Analysis (passive) ===",
             f"Access points seen: {result['total_aps']}", ""]

    for band in ("2.4", "5", "6"):
        info = result["bands"].get(band)
        if info is None or (band == "6" and info["ap_count"] == 0):
            continue
        lines.append(f"-- {band} GHz --  {info['ap_count']} AP(s), "
                     f"{info['hidden']} hidden, {info['open_networks']} open")
        for channel, count in info["channels"].items():
            lines.append(f"  ch {channel:>3}: {_bar(count):<40} {count}")
        strongest = info["strongest"]
        if strongest:
            gen = f", {strongest['generation']}" if strongest.get("generation") else ""
            lines.append(f"  strongest: {strongest['ssid']} "
                         f"({strongest['signal_dbm']} dBm, ch {strongest['channel']}{gen})")
        lines.append("")

    lines.append(f"Least-congested 2.4 GHz channel: {result['recommended_24_channel']} "
                 f"(scores {result['channel_24_congestion']})")
    return "\n".join(lines)
