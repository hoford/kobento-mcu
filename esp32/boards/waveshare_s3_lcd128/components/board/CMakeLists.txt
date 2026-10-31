# The Waveshare ESP32-S3 1.28" round board's `board` component: the shared board interface
# (board/board.h) with small drivers (GC9A01A over esp_lcd SPI, CST816S over I2C, LEDC
# backlight, battery ADC).
idf_component_register(SRCS src/board.cpp
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_i2c esp_driver_uart
                                     esp_driver_gpio esp_driver_ledc esp_adc esp_timer mbedtls)
