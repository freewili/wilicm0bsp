# aarch64 cross-compile toolchain for the Raspberry Pi CM0 target.
#
# Usage:
#   cmake -S . -B build-pi -DFWCM0_TARGET=ON \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/pi-toolchain.cmake \
#         -DPI_SYSROOT=/path/to/pi/sysroot
#   cmake --build build-pi
#
# The sysroot must contain the Pi's libgpiod (gpiod.h + libgpiod.so) and the
# usual aarch64 runtime. Override the compiler/sysroot via -D as needed.
#
# Alternative: build NATIVELY on the Pi with no toolchain file at all:
#   sudo apt install libgpiod-dev
#   cmake -S . -B build -DFWCM0_TARGET=ON && cmake --build build

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
endif()

if(DEFINED PI_SYSROOT)
  set(CMAKE_SYSROOT ${PI_SYSROOT})
  set(CMAKE_FIND_ROOT_PATH ${PI_SYSROOT})
endif()

# Find headers/libs in the target sysroot, programs on the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
