# The M5Stack Core2's `board` component: the shared board interface (board/board.h) with its
# own small drivers (ILI9342C over esp_lcd SPI, FT6336U and AXP192/AXP2101 over I2C).
idf_component_register(SRCS src/display.cpp src/power.cpp
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_uart
                                     esp_driver_gpio esp_timer mbedtls)
