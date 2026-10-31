# The Elecrow CrowPanel 5.79" e-paper's `board` component: the shared board interface
# (board/board.h) with a small driver for its two SSD1683 controllers over SPI, and the five
# keys on the right edge.
idf_component_register(SRCS src/board.cpp
                       INCLUDE_DIRS include
                       PRIV_REQUIRES esp_driver_spi esp_driver_gpio esp_driver_uart esp_timer mbedtls)
