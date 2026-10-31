# macOS/Linux host build: the same rccore + ThorVG backend the Tab5 runs, for fast
# iteration and pixel comparison against the Skia reference player.
cmake_minimum_required(VERSION 3.20)
project(m5tab_host CXX C)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
add_subdirectory(../../components/thorvg thorvg)
add_subdirectory(../../components/rccore rccore)
add_subdirectory(../../components/kobentovg kobentovg)

find_package(ZLIB REQUIRED)
add_executable(rc2png rc2png/main.cpp)
target_link_libraries(rc2png PRIVATE kobentovg ZLIB::ZLIB)
target_compile_definitions(rc2png PRIVATE FONT_DIR="${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts")

# paintcheck: colour filters and JPEG decoding in the ThorVG backend, checked pixel by pixel.
add_executable(paintcheck paintcheck/main.cpp)
target_link_libraries(paintcheck PRIVATE kobentovg ZLIB::ZLIB)
target_compile_definitions(paintcheck PRIVATE FONT_DIR="${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts")
