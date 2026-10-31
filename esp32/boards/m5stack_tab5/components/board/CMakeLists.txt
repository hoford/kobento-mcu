# The M5Stack Tab5's `board` component: the shared board interface (board/board.h) over the
# espressif/m5stack_tab5 BSP.
idf_component_register(SRCS src/display.cpp src/power.cpp src/extras.cpp
                       INCLUDE_DIRS include
                       PRIV_REQUIRES m5stack_tab5 esp_timer esp_lcd esp_driver_ppa esp_driver_i2c esp_driver_usb_serial_jtag esp_mm
                                     mbedtls)
