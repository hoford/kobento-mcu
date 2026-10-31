# The M5Stack PaperS3's `board` component: the shared board interface (board/board.h) with
# small drivers (ED047TC1 e-ink over the LCD peripheral's i80 mode, GT911 over I2C, battery
# ADC, BMI270, BM8563 RTC, buzzer).
idf_component_register(SRCS src/board.cpp
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_usb_serial_jtag
                                     esp_adc esp_timer esp_driver_ledc mbedtls)
