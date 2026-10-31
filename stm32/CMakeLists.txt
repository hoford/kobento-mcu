cmake_minimum_required(VERSION 3.20)
project(stm32u5_rc C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(RC_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
add_subdirectory(${RC_ROOT}/components/thorvg thorvg)
add_subdirectory(${RC_ROOT}/components/rccore rccore)
add_subdirectory(${RC_ROOT}/components/kobentovg kobentovg)
# No threads here: see src/single_thread_mutex.h.
foreach(t thorvg rccore kobentovg)
    target_compile_options(${t} PRIVATE "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_CURRENT_LIST_DIR}/src/single_thread_mutex.h>")
endforeach()
# rccore takes the UTC offset from tm_gmtoff, which newlib's struct tm lacks. Its newlib
# path (_timezone from tzset) is guarded by ESP_PLATFORM: take that path. Reported upstream.
target_compile_definitions(rccore PRIVATE ESP_PLATFORM=1)

# ── The firmware ──────────────────────────────────────────────────────────────────────
set(CUBE ${CMAKE_CURRENT_LIST_DIR}/../../STM32CubeU5 CACHE PATH "STM32CubeU5 checkout")
set(FONTS ${RC_ROOT}/assets/fonts)
set(DOCS ${RC_ROOT}/documents)

# ST's HAL and this board's BSP (LCD, touch, button, LEDs, log port).
file(GLOB HAL_SRCS ${CUBE}/Drivers/STM32U5xx_HAL_Driver/Src/*.c)
list(FILTER HAL_SRCS EXCLUDE REGEX "_template\\.c$")
set(BSP ${CUBE}/Drivers/BSP/STM32U5x9J-DK)
add_library(cube STATIC ${HAL_SRCS}
    ${BSP}/stm32u5x9j_discovery.c ${BSP}/stm32u5x9j_discovery_bus.c
    ${BSP}/stm32u5x9j_discovery_lcd.c ${BSP}/stm32u5x9j_discovery_ts.c ${BSP}/stm32u5x9j_discovery_hspi.c
    ${CUBE}/Drivers/BSP/Components/aps512xx/aps512xx.c
    ${CUBE}/Drivers/BSP/Components/sitronix/sitronix.c ${CUBE}/Drivers/BSP/Components/sitronix/sitronix_reg.c
    board/system_stm32u5xx.c)
target_include_directories(cube PUBLIC board ${BSP} ${CUBE}/Drivers/BSP/Components/Common ${CUBE}/Drivers/BSP/Components/aps512xx
    ${CUBE}/Drivers/STM32U5xx_HAL_Driver/Inc ${CUBE}/Drivers/CMSIS/Device/ST/STM32U5xx/Include
    ${CUBE}/Drivers/CMSIS/Include)
target_compile_definitions(cube PUBLIC STM32U5A9xx USE_HAL_DRIVER)
target_compile_options(cube PRIVATE -O2 -w)

# Fonts and documents, linked in from their files (embedded.S, embedded.h).
set(EMBED_DOCS
    muse_splash activity_rings balls_animation_example dashboard double_pendulum_example flappy
    good_pie_chart moon_phase_dial server_clock touch2)
set(EMBED_FIT friendly_aliens_ufo gmt_watch jancy_clock maze maze_simple sakura_season
    solar_gmt_watch under_the_sea)
set(ASM "")
set(HDR "#pragma once\n#include <cstddef>\n#include <cstdint>\nextern \"C\" {\n")
set(TABLE "")
macro(embed sym path)
    string(APPEND ASM ".section .rodata.${sym}\n.balign 4\n.global ${sym}_start\n.global ${sym}_end\n${sym}_start:\n.incbin \"${path}\"\n${sym}_end:\n.byte 0\n")
    string(APPEND HDR "extern const uint8_t ${sym}_start[], ${sym}_end[];\n")
endmacro()
embed(font_sans ${FONTS}/Roboto-Regular.ttf)
embed(font_sans_bold ${FONTS}/Roboto-Bold.ttf)
embed(font_mono ${FONTS}/RobotoMono-Regular.ttf)
foreach(d ${EMBED_DOCS})
    embed(doc_${d} ${DOCS}/${d}.rc)
    string(APPEND TABLE "    {\"${d}\", doc_${d}_start, doc_${d}_end, false},\n")
endforeach()
foreach(d ${EMBED_FIT})
    embed(doc_${d} ${DOCS}/fit/${d}.rc)
    string(APPEND TABLE "    {\"${d}\", doc_${d}_start, doc_${d}_end, true},\n")
endforeach()
string(APPEND HDR "}\nstruct EmbeddedDoc { const char* name; const uint8_t* start; const uint8_t* end; bool fit; };\n"
    "static const EmbeddedDoc kEmbeddedDocs[] = {\n${TABLE}};\n"
    "static constexpr size_t kEmbeddedDocCount = sizeof kEmbeddedDocs / sizeof kEmbeddedDocs[0];\n"
    "#define kFontSans font_sans_start\n#define kFontSansSize ((size_t) (font_sans_end - font_sans_start))\n"
    "#define kFontSansBold font_sans_bold_start\n#define kFontSansBoldSize ((size_t) (font_sans_bold_end - font_sans_bold_start))\n"
    "#define kFontMono font_mono_start\n#define kFontMonoSize ((size_t) (font_mono_end - font_mono_start))\n")
file(WRITE ${CMAKE_BINARY_DIR}/embedded.S "${ASM}")
file(WRITE ${CMAKE_BINARY_DIR}/embedded.h "${HDR}")

string(TIMESTAMP BUILD_EPOCH "%s" UTC)
add_executable(rcplayer.elf src/main.cpp src/runtime.c board/board.c
    board/startup_stm32u5a9njhxq.s ${CMAKE_BINARY_DIR}/embedded.S)
target_include_directories(rcplayer.elf PRIVATE ${CMAKE_BINARY_DIR} board)
target_compile_definitions(rcplayer.elf PRIVATE BUILD_EPOCH=${BUILD_EPOCH}LL)
target_compile_options(rcplayer.elf PRIVATE -O2 -Wall
    "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${CMAKE_CURRENT_LIST_DIR}/src/single_thread_mutex.h>")
target_link_libraries(rcplayer.elf PRIVATE kobentovg cube)
target_link_options(rcplayer.elf PRIVATE -T${CMAKE_CURRENT_LIST_DIR}/board/stm32u5a9_flash.ld
    --specs=nosys.specs -u _printf_float -Wl,-Map=${CMAKE_BINARY_DIR}/rcplayer.map -Wl,--print-memory-usage)
set_property(SOURCE ${CMAKE_BINARY_DIR}/embedded.S APPEND PROPERTY OBJECT_DEPENDS
    ${FONTS}/Roboto-Regular.ttf)
add_custom_command(TARGET rcplayer.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary rcplayer.elf rcplayer.bin
    COMMAND ${CMAKE_SIZE} rcplayer.elf)
