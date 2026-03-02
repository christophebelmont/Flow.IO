#pragma once

#include <stdint.h>

#include "Board/BoardPinMap.h"
#include "Core/SystemLimits.h"

struct DigitalOutDef {
    const char* name;
    uint8_t pin;
    bool momentary;
    uint16_t pulseMs;
};

struct DigitalInDef {
    const char* name;
    uint8_t pin;
};

namespace BoardLayout {

constexpr DigitalOutDef DOs[] = {
    {"filtration", Board::DO::Filtration, false, 0},
    {"ph_pump", Board::DO::PhPump, false, 0},
    {"chlorine_pump", Board::DO::ChlorinePump, false, 0},
    {"chlorine_generator", Board::DO::ChlorineGenerator, true, Limits::MomentaryPulseMs},
    {"robot", Board::DO::Robot, false, 0},
    {"lights", Board::DO::Lights, false, 0},
    {"fill_pump", Board::DO::FillPump, false, 0},
    {"water_heater", Board::DO::WaterHeater, false, 0},
};

constexpr DigitalInDef DIs[] = {
    {"Pool Level", Board::DI::FlowSwitch},
    {"pH Level", Board::DI::PhLevel},
    {"Chlorine Level", Board::DI::ChlorineLevel},
};

constexpr uint8_t DigitalOutCount = (uint8_t)(sizeof(DOs) / sizeof(DOs[0]));
constexpr uint8_t DigitalInCount = (uint8_t)(sizeof(DIs) / sizeof(DIs[0]));

}  // namespace BoardLayout
