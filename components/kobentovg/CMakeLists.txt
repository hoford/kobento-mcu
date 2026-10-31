# ThorVG-backed RemoteCompose PaintContext.
if(ESP_PLATFORM)
    idf_component_register(SRCS src/ThorvgPaintContext.cpp
                           INCLUDE_DIRS include
                           REQUIRES rccore thorvg)
    target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
else()
    add_library(kobentovg STATIC src/ThorvgPaintContext.cpp)
    target_include_directories(kobentovg PUBLIC include)
    target_link_libraries(kobentovg PUBLIC rccore thorvg)
    target_compile_features(kobentovg PUBLIC cxx_std_17)
endif()
