"""Ten read-only GPIO snapshots. Requires the FPGA power zone to be enabled."""
import time
from onewili_cm0 import connect_cm0


def main():
    with connect_cm0() as device:
        for _ in range(10):
            print(f"GPIO: 0x{device.io.gpio.read_all().unwrap():08X}", flush=True)
            time.sleep(0.5)


if __name__ == "__main__":
    main()
