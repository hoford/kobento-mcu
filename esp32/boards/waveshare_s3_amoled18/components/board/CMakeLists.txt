# The Waveshare ESP32-S3-Touch-AMOLED-1.8's `board` component: the shared board interface
# (board/board.h) with small drivers (the AMOLED over esp_lcd QSPI, FT3168 / CST816 touch and
# the AXP2101 PMU over I2C).
idf_component_register(SRCS src/board.cpp
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_gpio
                                     esp_driver_usb_serial_jtag esp_timer mbedtls)
