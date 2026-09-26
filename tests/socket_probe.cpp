#include "wilicm0/device.hpp"
#include <cstdio>
#include <exception>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        wilicm0::Device device(argv[1]);
        uint32_t gpio = 0;
        auto status = ow_io_gpio_read_all(device.get(), &gpio);
        if (status != OW_OK) return 3;
        std::printf("GPIO=%08X\n", gpio);
        return gpio == 0x12345678 ? 0 : 4;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what()); return 1;
    }
}
