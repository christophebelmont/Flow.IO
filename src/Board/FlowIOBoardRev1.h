#pragma once

#include "Board/BoardSpec.h"
#include "Core/SystemLimits.h"

namespace BoardProfiles {

inline constexpr UartSpec kFlowIOBoardRev1Uarts[] = {
    {"log", 0, -1, -1, 115200, true},
    {"hmi", 2, 16, 17, 115200, false},
};

inline constexpr I2cBusSpec kFlowIOBoardRev1I2c[] = {
    {"io", 21, 22, 400000},
};

inline constexpr OneWireBusSpec kFlowIOBoardRev1OneWire[] = {
    {"temp_probe_1", BoardSignal::TempProbe1, 19},
    {"temp_probe_2", BoardSignal::TempProbe2, 18},
};

inline constexpr IoPointSpec kFlowIOBoardRev1IoPoints[] = {
    {"relay1", IoCapability::DigitalOut, BoardSignal::Relay1, 32, false, 0},
    {"relay2", IoCapability::DigitalOut, BoardSignal::Relay2, 25, false, 0},
    {"relay3", IoCapability::DigitalOut, BoardSignal::Relay3, 26, false, 0},
    {"relay4", IoCapability::DigitalOut, BoardSignal::Relay4, 13, true, Limits::MomentaryPulseMs},
    {"relay5", IoCapability::DigitalOut, BoardSignal::Relay5, 33, false, 0},
    {"relay6", IoCapability::DigitalOut, BoardSignal::Relay6, 27, false, 0},
    {"relay7", IoCapability::DigitalOut, BoardSignal::Relay7, 23, false, 0},
    {"relay8", IoCapability::DigitalOut, BoardSignal::Relay8, 4, false, 0},
    {"digital_in1", IoCapability::DigitalIn, BoardSignal::DigitalIn1, 34, false, 0},
    {"digital_in2", IoCapability::DigitalIn, BoardSignal::DigitalIn2, 36, false, 0},
    {"digital_in3", IoCapability::DigitalIn, BoardSignal::DigitalIn3, 39, false, 0},
    {"digital_in4", IoCapability::DigitalIn, BoardSignal::DigitalIn4, 35, false, 0},
    {"analog_in1", IoCapability::AnalogIn, BoardSignal::AnalogIn1, 0, false, 0},
    {"analog_in2", IoCapability::AnalogIn, BoardSignal::AnalogIn2, 1, false, 0},
    {"analog_in3", IoCapability::AnalogIn, BoardSignal::AnalogIn3, 2, false, 0},
    {"analog_in4", IoCapability::AnalogIn, BoardSignal::AnalogIn4, 3, false, 0},
    {"temp_probe_1", IoCapability::OneWireTemp, BoardSignal::TempProbe1, 19, false, 0},
    {"temp_probe_2", IoCapability::OneWireTemp, BoardSignal::TempProbe2, 18, false, 0},
};

inline constexpr BoardSpec kFlowIOBoardRev1{
    "FlowIOBoardRev1",
    kFlowIOBoardRev1Uarts,
    (uint8_t)(sizeof(kFlowIOBoardRev1Uarts) / sizeof(kFlowIOBoardRev1Uarts[0])),
    kFlowIOBoardRev1I2c,
    (uint8_t)(sizeof(kFlowIOBoardRev1I2c) / sizeof(kFlowIOBoardRev1I2c[0])),
    kFlowIOBoardRev1OneWire,
    (uint8_t)(sizeof(kFlowIOBoardRev1OneWire) / sizeof(kFlowIOBoardRev1OneWire[0])),
    kFlowIOBoardRev1IoPoints,
    (uint8_t)(sizeof(kFlowIOBoardRev1IoPoints) / sizeof(kFlowIOBoardRev1IoPoints[0])),
    nullptr
};

}  // namespace BoardProfiles
