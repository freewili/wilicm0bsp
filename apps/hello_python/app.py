"""Read-only first application; output appears in the Linux Apps log."""
from onewili_cm0 import connect_cm0


def main():
    with connect_cm0() as device:
        sd, streaming, mask, clock = device.hardware.system.device_state().unwrap()
        print(f"Hello from FreeWili 2 Linux! MAIN clock: {clock} Hz", flush=True)
        print(f"SD owner: {sd}; host streaming: {streaming}; active mask: {mask}", flush=True)


if __name__ == "__main__":
    main()
