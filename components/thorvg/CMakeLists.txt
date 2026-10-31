# ThorVG (vendored at third_party/thorvg) built for both targets:
#   - ESP-IDF component (ESP_PLATFORM set): `thorvg` component for the Tab5 firmware
#   - plain CMake static library for the macOS host tools
set(TVG_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../third_party/thorvg)

file(GLOB TVG_SRCS
    ${TVG_ROOT}/src/common/*.cpp
    ${TVG_ROOT}/src/renderer/*.cpp
    ${TVG_ROOT}/src/renderer/cpu_engine/*.cpp
    ${TVG_ROOT}/src/loaders/raw/*.cpp
    ${TVG_ROOT}/src/loaders/png/*.cpp
    ${TVG_ROOT}/src/loaders/jpg/*.cpp
    ${TVG_ROOT}/src/loaders/sfnt/*.cpp)

set(TVG_PRIV_INC
    ${CMAKE_CURRENT_LIST_DIR}
    ${TVG_ROOT}/src/common
    ${TVG_ROOT}/src/renderer
    ${TVG_ROOT}/src/renderer/cpu_engine
    ${TVG_ROOT}/src/loaders/raw
    ${TVG_ROOT}/src/loaders/png
    ${TVG_ROOT}/src/loaders/jpg
    ${TVG_ROOT}/src/loaders/sfnt)

set(TVG_FLAGS -DTVG_STATIC -Wno-unknown-pragmas -fno-math-errno -include ${CMAKE_CURRENT_LIST_DIR}/config.h)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${TVG_SRCS}
                           INCLUDE_DIRS ${TVG_ROOT}/inc
                           PRIV_INCLUDE_DIRS ${TVG_PRIV_INC})
    target_compile_options(${COMPONENT_LIB} PRIVATE ${TVG_FLAGS} -O2 -Wno-error -w)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC TVG_STATIC)
else()
    add_library(thorvg STATIC ${TVG_SRCS})
    target_include_directories(thorvg PUBLIC ${TVG_ROOT}/inc PRIVATE ${TVG_PRIV_INC})
    target_compile_options(thorvg PRIVATE ${TVG_FLAGS} -w)
    target_compile_definitions(thorvg PUBLIC TVG_STATIC)
    target_compile_features(thorvg PUBLIC cxx_std_14)
endif()
