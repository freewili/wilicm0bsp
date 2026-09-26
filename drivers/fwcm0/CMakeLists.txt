cmake_minimum_required(VERSION 3.16)
project(fwcm0 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(FWCM0_BUILD_TESTS "Build driver tests" OFF)
option(FWCM0_TARGET "Build the Pi target (LinuxTransport + CLI)" OFF)
option(FWCM0_EXAMPLES "Build the example programs" OFF)

# Core library (host-testable: everything above Transport)
add_library(fwcm0_core
  src/crc8.cpp src/cobs.cpp src/commands.cpp
  src/frame_parser.cpp src/uart_receiver.cpp src/sram_router.cpp
  src/console_client.cpp src/console_session.cpp src/mbox_mux.cpp src/shell_server.cpp
  src/bridge_proto.cpp)
target_include_directories(fwcm0_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
find_package(Threads REQUIRED)
target_link_libraries(fwcm0_core PUBLIC Threads::Threads)
add_library(fwcm0::fwcm0_core ALIAS fwcm0_core)

set(FWCM0_INSTALL_TARGETS fwcm0_core)

if(FWCM0_TARGET)
  # Target build (Linux / Raspberry Pi): hardware transport + CLI.
  # Uses Linux-only APIs (spidev, libgpiod, termios2) - never built on the host.
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The hardware driver requires Linux; use FWCM0_TARGET=OFF for host tests")
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GPIOD REQUIRED IMPORTED_TARGET libgpiod>=2.0)
  add_library(fwcm0_hw STATIC src/linux_transport.cpp)
  target_include_directories(fwcm0_hw PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_link_libraries(fwcm0_hw PUBLIC fwcm0_core PkgConfig::GPIOD)
  add_library(fwcm0::fwcm0_hw ALIAS fwcm0_hw)
  list(APPEND FWCM0_INSTALL_TARGETS fwcm0_hw)

  add_executable(fwcm0 cli/main.cpp src/console_cli.cpp src/bridge_daemon.cpp
    src/router_access_direct.cpp src/router_access_socket.cpp)
  target_include_directories(fwcm0 PRIVATE src)
  target_link_libraries(fwcm0 PRIVATE fwcm0_hw util)
  install(TARGETS fwcm0 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(FWCM0_BUILD_TESTS)
  enable_testing()
  # Pinned public dependency; no sibling firmware checkout is required.
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # match MSVC /MD; avoid CRT mismatch
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571)
  FetchContent_MakeAvailable(googletest)
  add_executable(fwcm0_tests
    tests/test_smoke.cpp
    tests/test_protocol.cpp
    tests/test_crc8.cpp
    tests/test_cobs.cpp
    tests/test_commands.cpp
    tests/test_frame_parser.cpp
    tests/test_mock_transport.cpp
    tests/test_uart_receiver.cpp
    tests/test_sram_router.cpp
    tests/test_console_client.cpp
    tests/test_console_session.cpp
    tests/test_mbox_mux.cpp
    tests/test_shell_server.cpp
    tests/test_bridge_proto.cpp)
  target_link_libraries(fwcm0_tests PRIVATE fwcm0_core gtest_main)
  target_include_directories(fwcm0_tests PRIVATE tests src)
  add_test(NAME fwcm0_tests COMMAND fwcm0_tests)
endif()

# Example applications. hello_mock is host-buildable (MockTransport); the
# others drive real hardware and need the target build.
if(FWCM0_EXAMPLES)
  add_executable(ex_hello_mock examples/hello_mock.cpp)
  target_link_libraries(ex_hello_mock PRIVATE fwcm0_core)
  if(FWCM0_TARGET)
    add_executable(ex_producer_loop examples/producer_loop.cpp)
    target_link_libraries(ex_producer_loop PRIVATE fwcm0_hw)
    add_executable(ex_notify_monitor examples/notify_monitor.cpp)
    target_link_libraries(ex_notify_monitor PRIVATE fwcm0_hw)
    add_executable(ex_console_test examples/console_test.cpp)
    target_link_libraries(ex_console_test PRIVATE fwcm0_hw)
  endif()
endif()

# Install / export so external projects can find_package(fwcm0) and link
# fwcm0::fwcm0_core (host) or fwcm0::fwcm0_hw (target).
install(TARGETS ${FWCM0_INSTALL_TARGETS}
  EXPORT fwcm0Targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT fwcm0Targets
  FILE fwcm0Targets.cmake
  NAMESPACE fwcm0::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fwcm0)
configure_package_config_file(
  cmake/fwcm0-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/fwcm0-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fwcm0)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/fwcm0-config-version.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/fwcm0-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/fwcm0-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fwcm0)
