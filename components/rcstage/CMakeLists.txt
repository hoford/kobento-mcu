# rcstage: one RemoteCompose document live on a board's screen (player task, touch, banner,
# system/named variables, host actions) and the device-state sampler. Hardware comes from
# the selected board's `board` component (board/board.h). Shared by firmware/rcplayer and
# firmware/muse-gadget.
set(FONT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../assets/fonts)
idf_component_register(SRCS src/stage.cpp src/device_state.cpp
                       INCLUDE_DIRS include
                       REQUIRES rccore kobentovg thorvg
                       PRIV_REQUIRES board esp_timer esp_driver_tsens esp_wifi esp_netif
                       EMBED_FILES ${FONT_DIR}/Roboto-Regular.ttf ${FONT_DIR}/Roboto-Bold.ttf
                                   ${FONT_DIR}/RobotoMono-Regular.ttf)
