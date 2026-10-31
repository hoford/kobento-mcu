// Core2 internals shared by display.cpp and power.cpp: the internal I2C bus and the PMU.
#pragma once
#include <cstdint>

#include "driver/i2c_master.h"

namespace board {

// Internal I2C (SDA 21, SCL 22, 400 kHz): PMU 0x34, touch 0x38, RTC 0x51, IMU 0x68,
// and on v1.1 an INA3221 at 0x40.
i2c_master_bus_handle_t i2cBus();
i2c_master_dev_handle_t i2cDevice(uint8_t addr);   // added once, then cached
bool i2cRead(i2c_master_dev_handle_t d, uint8_t reg, uint8_t* out, size_t n);
bool i2cWrite(i2c_master_dev_handle_t d, uint8_t reg, uint8_t value);

// The power chip. v1.0 boards have an AXP192, v1.1 an AXP2101 (register 0x03 reads 0x03 or
// 0x4A). pmuInit() powers the LCD, touch and SD rails and resets the LCD.
enum class Pmu { None, Axp192, Axp2101 };
Pmu pmuInit();
Pmu pmu();
void pmuBacklight(int percent);   // 0 = off

// The three marks printed under the screen (A, B, C) are touch rows 240..279. readTouch()
// keeps them off the document and records a press: -1 for A, +1 for B or C, once per touch.
int touchMarkStep();   // the step pressed since the last call, else 0

}  // namespace board
