# CMake toolchain for cross-compiling the Linux player to a Raspberry Pi from another machine.
#
#   cmake -S linux -B linux/build-pi -G Ninja -DCMAKE_TOOLCHAIN_FILE=linux/cmake/rpi-cross.cmake
#   cmake --build linux/build-pi
#
# Point it at your cross toolchain and the Pi's sysroot through environment variables (no paths
# are baked in here). A Linux (glibc) cross GCC — NOT the bare-metal arm-none-eabi toolchain the
# STM32 build uses:
#
#   export RPI_SYSROOT=/path/to/raspios-sysroot        # the Pi's / copied locally (has libz etc.)
#   export RPI_TOOLCHAIN=arm-linux-gnueabihf           # triple, or an absolute prefix path
#   # Pi Zero / Zero W (ARMv6, hard-float, no NEON):
#   export RPI_ARCH_FLAGS="-march=armv6 -mfpu=vfp -mfloat-abi=hard"
#   # Pi Zero 2 W 32-bit (Cortex-A53, has NEON): "-march=armv8-a+crc -mfpu=neon-fp-armv8 -mfloat-abi=hard"
#   # Pi Zero 2 W 64-bit: use RPI_TOOLCHAIN=aarch64-linux-gnu and leave RPI_ARCH_FLAGS empty.
#
# The simplest alternative to all of this is to build natively on the Pi itself (slow on a Zero
# but needs no toolchain file): see README.md.

set(CMAKE_SYSTEM_NAME Linux)

if(DEFINED ENV{RPI_TOOLCHAIN})
    set(_tc $ENV{RPI_TOOLCHAIN})
else()
    set(_tc arm-linux-gnueabihf)
endif()

# A triple (arm-linux-gnueabihf) resolves on PATH; an absolute prefix (…/bin/arm-linux-gnueabihf)
# is used as given.
set(CMAKE_C_COMPILER   ${_tc}-gcc)
set(CMAKE_CXX_COMPILER ${_tc}-g++)

if(_tc MATCHES "aarch64")
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
else()
    set(CMAKE_SYSTEM_PROCESSOR arm)
endif()

if(DEFINED ENV{RPI_SYSROOT})
    set(CMAKE_SYSROOT $ENV{RPI_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH $ENV{RPI_SYSROOT})
endif()

if(DEFINED ENV{RPI_ARCH_FLAGS})
    set(CMAKE_C_FLAGS_INIT   "$ENV{RPI_ARCH_FLAGS}")
    set(CMAKE_CXX_FLAGS_INIT "$ENV{RPI_ARCH_FLAGS}")
endif()

# Find libraries/headers in the sysroot, but programs (the compiler) on the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
