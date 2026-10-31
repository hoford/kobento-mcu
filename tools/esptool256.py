#!/usr/bin/env python3
# esptool with 256-byte ROM write blocks, for boards whose USB bridge corrupts larger
# packets: the Waveshare ESP32-S3 1.28" (CH343P on macOS's CDC driver) fails at 512 bytes
# and its stub upload fails outright. Run with IDF's Python, and pass --no-stub:
#   $IDF_PYTHON tools/esptool256.py --chip esp32s3 -p PORT -b 460800 --no-stub write-flash @flash_args
import sys
import esptool
from esptool.loader import ESPLoader
from esptool.targets import ESP32S3ROM
for cls in (ESPLoader, ESP32S3ROM):
    cls.FLASH_WRITE_SIZE = 0x100
    cls.ESP_RAM_BLOCK = 0x100
sys.argv = ["esptool"] + sys.argv[1:]
esptool._main()
