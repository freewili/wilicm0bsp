#include "wilicm0/device.hpp"
#include <cstdio>
#include <exception>

int main() {
    try {
        wilicm0::Device device;
        char sd[32]{}, mask[32]{}; bool streaming = false; int32_t clock = 0;
        const auto status = ow_hardware_system_device_state(device.get(), sd, sizeof(sd), &streaming, mask, sizeof(mask), &clock);
        if (status != OW_OK) {
            std::fprintf(stderr, "Device State failed: %d %s\n", status, device.error().c_str());
            return 1;
        }
        std::printf("FreeWili 2 MAIN: SD=%s, clock=%d Hz, host streaming=%s\n", sd, clock, streaming ? "on" : "off");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what()); return 1;
    }
}
