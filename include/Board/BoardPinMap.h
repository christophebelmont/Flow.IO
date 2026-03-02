#pragma once

#include <stdint.h>

#ifndef BOARD_REV
#define BOARD_REV 1
#endif

namespace Board {

//ESP32 unusable pins: 0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12 (boot)
//FLOW.IO uses UART pins 
#if BOARD_REV == 1
namespace DO {
constexpr uint8_t Filtration = 32;
constexpr uint8_t PhPump = 25;
constexpr uint8_t ChlorinePump = 26;
constexpr uint8_t ChlorineGenerator = 13;
constexpr uint8_t Robot = 33;
constexpr uint8_t Lights = 27;
constexpr uint8_t FillPump = 23;
constexpr uint8_t WaterHeater = 4;
}  // namespace DO

// Digital input-only pins are 34, 35, 36, 39.
namespace DI {
constexpr uint8_t FlowSwitch = 34;
constexpr uint8_t PhLevel = 36;
constexpr uint8_t ChlorineLevel = 39;
}  // namespace DI

namespace OneWire {
constexpr uint8_t BusA = 19;
constexpr uint8_t BusB = 18;
}  // namespace OneWire

namespace I2C {
constexpr uint8_t Sda = 21;
constexpr uint8_t Scl = 22;
}  // namespace I2C
#else
#error "Unsupported BOARD_REV value"
#endif

}  // namespace Board
