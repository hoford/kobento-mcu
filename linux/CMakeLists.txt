# Kobento RC player for Linux (Raspberry Pi and desktop).
#
# A display-only RemoteCompose player built on the same rccore + kobentovg (ThorVG) engine the
# ESP32/STM32 players run — the host build path, not ESP-IDF. It renders documents to the Linux
# framebuffer (/dev/fb0), or runs headless (snap to PNG) on a machine without one, and takes the
# same console commands as esp32/firmware/rcplayer.
#
#   cmake -S linux -B linux/build -G Ninja
#   cmake --build linux/build
#   linux/build/rcplayer documents            # play every .rc in documents/
#
# Cross-compile for a Pi with a toolchain file:
#   cmake -S linux -B linux/build-pi -G Ninja -DCMAKE_TOOLCHAIN_FILE=linux/cmake/rpi-cross.cmake
#
# rccore comes from the sibling remotecompose-experiments checkout (override -DRC_EXPERIMENTS=…),
# exactly as esp32/host does.
cmake_minimum_required(VERSION 3.20)
project(kobento_linux CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The same three components the host renderer builds (each has a non-ESP add_library branch).
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../components/thorvg thorvg)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../components/rccore rccore)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../components/kobentovg kobentovg)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
# SDL2 gives the desktop window (Wayland/PIXEL or X11). Optional: without it the player still
# builds and runs headless (snap to PNG), which is how it is smoke-tested on a box with no
# display.
find_package(SDL2 QUIET)

add_executable(rcplayer
    src/main.cpp
    src/player.cpp
    src/display.cpp
    src/docpanel.cpp)
target_link_libraries(rcplayer PRIVATE kobentovg ZLIB::ZLIB Threads::Threads)
target_compile_definitions(rcplayer PRIVATE
    FONT_DIR="${CMAKE_CURRENT_LIST_DIR}/../assets/fonts")

# 32-bit ARM (Pi Zero / ARMv6) can't lower 64-bit atomics inline — rccore's 3D context uses
# them (__atomic_*_8), so they resolve from libatomic. Two wrinkles: the .so dev symlink isn't
# installed (only libatomic.so.1) and the real libatomic.so lives in gcc's own lib dir, so pass
# the raw -latomic flag and let the compiler driver find it (find_library only searches system
# dirs and misses it); and GNU ld resolves archives left-to-right, so -latomic must come AFTER
# librccore.a — target_link_libraries drops it mid-list, before librccore, where it does nothing.
# Appending to CMAKE_CXX_STANDARD_LIBRARIES puts it at the very end of the link line. Not
# needed/available on x86-64 or macOS, so guard to 32-bit ARM Linux.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SIZEOF_VOID_P EQUAL 4
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm")
    string(APPEND CMAKE_CXX_STANDARD_LIBRARIES " -latomic")
    message(STATUS "32-bit ARM: appending -latomic (end of link line) for 64-bit atomics")
endif()

if(SDL2_FOUND)
    target_link_libraries(rcplayer PRIVATE SDL2::SDL2)
    target_compile_definitions(rcplayer PRIVATE KOBENTO_HAVE_SDL)
    message(STATUS "SDL2 found (${SDL2_VERSION}): window backend enabled")
else()
    message(STATUS "SDL2 not found: building headless only (use --once / snap)")
endif()
