"""Replace this read-only example with your application."""
from onewili_cm0 import connect_cm0


def main():
    with connect_cm0() as device:
        print("Device:", device.hardware.system.device_state().unwrap(), flush=True)


if __name__ == "__main__":
    main()
