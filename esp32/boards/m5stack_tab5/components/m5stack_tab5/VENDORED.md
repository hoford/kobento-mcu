# Vendored espressif/m5stack_tab5 1.3.2 (esp-bsp f259a6a), trimmed

Why: the registry BSP depends on esp_lvgl_port 2.x, which does not build on ESP-IDF v6.0.1
(it uses `on_frame_buf_complete`, but v6.0.1's DPI callbacks still name it `on_refresh_done`),
and managed dependencies can't be excluded. This player renders with ThorVG and needs no LVGL.

Changes from upstream:
- removed `src/bsp_camera.c`, `src/bsp_usb.c` and the esp_lvgl_port / esp_video / usb deps
- `BSP_CONFIG_NO_GRAPHIC_LIB=1` set publicly in CMakeLists.txt
Display (all three panel revisions), touch, I2C, IO expanders, audio, IMU, SD are unchanged.
