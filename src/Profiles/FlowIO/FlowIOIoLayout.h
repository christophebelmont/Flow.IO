#pragma once

#include "Board/FlowIOBoardRev1.h"
#include "Domain/DomainTypes.h"
#include "Modules/IOModule/IODrivers/Ads1115Driver.h"
#include "Modules/IOModule/IOModuleTypes.h"

namespace Profiles {
namespace FlowIO {
namespace IoLayout {

enum : PhysicalPortId {
    PortAdsInternal0 = 100,
    PortAdsInternal1 = 101,
    PortAdsInternal2 = 102,
    PortAdsInternal3 = 103,
    PortAdsExternal0 = 110,
    PortAdsExternal1 = 111,
    PortDsWater = 120,
    PortDsAir = 121,
    PortSht40Temp = 130,
    PortSht40Humidity = 131,
    PortBmp280Temp = 132,
    PortBmp280Pressure = 133,
    PortBme680Temp = 134,
    PortBme680Humidity = 135,
    PortBme680Pressure = 136,
    PortBme680Gas = 137,
    PortIna226ShuntMv = 138,
    PortIna226BusV = 139,
    PortIna226CurrentMa = 140,
    PortIna226PowerMw = 141,
    PortIna226LoadV = 142,
    PortDigitalIn1 = 200,
    PortDigitalIn2 = 201,
    PortDigitalIn3 = 202,
    PortDigitalIn4 = 203,
    PortRelay1 = 300,
    PortRelay2 = 301,
    PortRelay3 = 302,
    PortRelay4 = 303,
    PortRelay5 = 304,
    PortRelay6 = 305,
    PortRelay7 = 306,
    PortRelay8 = 307,
    PortPcf0Bit0 = 400,
    PortPcf0Bit1 = 401,
    PortPcf0Bit2 = 402,
    PortPcf0Bit3 = 403,
    PortPcf0Bit4 = 404,
    PortPcf0Bit5 = 405,
    PortPcf0Bit6 = 406,
    PortPcf0Bit7 = 407
};

inline constexpr IOBindingPortSpec kBindingPorts[] = {
    {PortAdsInternal0, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 0, 0},
    {PortAdsInternal1, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 1, 0},
    {PortAdsInternal2, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 2, 0},
    {PortAdsInternal3, IO_PORT_KIND_ADS_INTERNAL_SINGLE, 3, 0},
    {PortAdsExternal0, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 0, 0},
    {PortAdsExternal1, IO_PORT_KIND_ADS_EXTERNAL_DIFF, 1, 0},
    {PortDsWater, IO_PORT_KIND_DS18_WATER, 0, 0},
    {PortDsAir, IO_PORT_KIND_DS18_AIR, 0, 0},
    {PortSht40Temp, IO_PORT_KIND_SHT40, 0, 0},
    {PortSht40Humidity, IO_PORT_KIND_SHT40, 1, 0},
    {PortBmp280Temp, IO_PORT_KIND_BMP280, 0, 0},
    {PortBmp280Pressure, IO_PORT_KIND_BMP280, 1, 0},
    {PortBme680Temp, IO_PORT_KIND_BME680, 0, 0},
    {PortBme680Humidity, IO_PORT_KIND_BME680, 1, 0},
    {PortBme680Pressure, IO_PORT_KIND_BME680, 2, 0},
    {PortBme680Gas, IO_PORT_KIND_BME680, 3, 0},
    {PortIna226ShuntMv, IO_PORT_KIND_INA226, 0, 0},
    {PortIna226BusV, IO_PORT_KIND_INA226, 1, 0},
    {PortIna226CurrentMa, IO_PORT_KIND_INA226, 2, 0},
    {PortIna226PowerMw, IO_PORT_KIND_INA226, 3, 0},
    {PortIna226LoadV, IO_PORT_KIND_INA226, 4, 0},
    {PortDigitalIn1, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[8].pin, 0},
    {PortDigitalIn2, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[9].pin, 0},
    {PortDigitalIn3, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[10].pin, 0},
    {PortDigitalIn4, IO_PORT_KIND_GPIO_INPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[11].pin, 0},
    {PortRelay1, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[0].pin, 0},
    {PortRelay2, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[1].pin, 0},
    {PortRelay3, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[2].pin, 0},
    {PortRelay4, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[3].pin, 0},
    {PortRelay5, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[4].pin, 0},
    {PortRelay6, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[5].pin, 0},
    {PortRelay7, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[6].pin, 0},
    {PortRelay8, IO_PORT_KIND_GPIO_OUTPUT, BoardProfiles::kFlowIOBoardRev1IoPoints[7].pin, 0},
    {PortPcf0Bit0, IO_PORT_KIND_PCF8574_OUTPUT, 0, 0},
    {PortPcf0Bit1, IO_PORT_KIND_PCF8574_OUTPUT, 1, 0},
    {PortPcf0Bit2, IO_PORT_KIND_PCF8574_OUTPUT, 2, 0},
    {PortPcf0Bit3, IO_PORT_KIND_PCF8574_OUTPUT, 3, 0},
    {PortPcf0Bit4, IO_PORT_KIND_PCF8574_OUTPUT, 4, 0},
    {PortPcf0Bit5, IO_PORT_KIND_PCF8574_OUTPUT, 5, 0},
    {PortPcf0Bit6, IO_PORT_KIND_PCF8574_OUTPUT, 6, 0},
    {PortPcf0Bit7, IO_PORT_KIND_PCF8574_OUTPUT, 7, 0},
};

constexpr PhysicalPortId analogPortFromLegacy(uint8_t source, uint8_t channel)
{
    switch (source) {
        case IO_SRC_ADS_INTERNAL_SINGLE:
            return (channel == 0U) ? PortAdsInternal0 :
                   (channel == 1U) ? PortAdsInternal1 :
                   (channel == 2U) ? PortAdsInternal2 :
                                     PortAdsInternal3;
        case IO_SRC_ADS_EXTERNAL_DIFF:
            return (channel == 0U) ? PortAdsExternal0 : PortAdsExternal1;
        case IO_SRC_DS18_WATER:
            return PortDsWater;
        case IO_SRC_DS18_AIR:
            return PortDsAir;
        default:
            return IO_PORT_INVALID;
    }
}

struct AnalogRoleDefault {
    DomainRole role;
    PhysicalPortId bindingPort;
    float c0;
    float c1;
    int32_t precision;
};

inline constexpr AnalogRoleDefault kAnalogRoleDefaults[] = {
    {DomainRole::OrpSensor, analogPortFromLegacy(FLOW_WIRDEF_IO_A0S, FLOW_WIRDEF_IO_A0C), FLOW_WIRDEF_IO_A00, FLOW_WIRDEF_IO_A01, FLOW_WIRDEF_IO_A0P},
    {DomainRole::PhSensor, analogPortFromLegacy(FLOW_WIRDEF_IO_A1S, FLOW_WIRDEF_IO_A1C), FLOW_WIRDEF_IO_A10, FLOW_WIRDEF_IO_A11, FLOW_WIRDEF_IO_A1P},
    {DomainRole::PsiSensor, analogPortFromLegacy(FLOW_WIRDEF_IO_A2S, FLOW_WIRDEF_IO_A2C), FLOW_WIRDEF_IO_A20, FLOW_WIRDEF_IO_A21, FLOW_WIRDEF_IO_A2P},
    {DomainRole::SpareAnalog, analogPortFromLegacy(FLOW_WIRDEF_IO_A3S, FLOW_WIRDEF_IO_A3C), FLOW_WIRDEF_IO_A30, FLOW_WIRDEF_IO_A31, FLOW_WIRDEF_IO_A3P},
    {DomainRole::WaterTemp, analogPortFromLegacy(FLOW_WIRDEF_IO_A4S, FLOW_WIRDEF_IO_A4C), FLOW_WIRDEF_IO_A40, FLOW_WIRDEF_IO_A41, FLOW_WIRDEF_IO_A4P},
    {DomainRole::AirTemp, analogPortFromLegacy(FLOW_WIRDEF_IO_A5S, FLOW_WIRDEF_IO_A5C), FLOW_WIRDEF_IO_A50, FLOW_WIRDEF_IO_A51, FLOW_WIRDEF_IO_A5P},
};

struct DigitalInputRoleDefault {
    DomainRole role;
    PhysicalPortId bindingPort;
    uint8_t mode;
    uint8_t edgeMode;
    uint32_t debounceUs;
};

inline constexpr DigitalInputRoleDefault kDigitalInputRoleDefaults[] = {
    {DomainRole::PoolLevelSensor, PortDigitalIn4, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {DomainRole::PhLevelSensor, PortDigitalIn2, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {DomainRole::ChlorineLevelSensor, PortDigitalIn3, IO_DIGITAL_INPUT_STATE, IO_EDGE_RISING, 0U},
    {DomainRole::WaterCounterSensor, PortDigitalIn1, IO_DIGITAL_INPUT_COUNTER, IO_EDGE_RISING, 50000U},
};

struct DigitalOutputRoleDefault {
    DomainRole role;
    PhysicalPortId bindingPort;
    bool activeHigh;
    bool momentary;
    uint16_t pulseMs;
};

inline constexpr DigitalOutputRoleDefault kDigitalOutputRoleDefaults[] = {
    {DomainRole::FiltrationPump, PortRelay1, false, false, 0U},
    {DomainRole::PhPump, PortRelay2, false, false, 0U},
    {DomainRole::ChlorinePump, PortRelay3, false, false, 0U},
    {DomainRole::ChlorineGenerator, PortRelay4, false, BoardProfiles::kFlowIOBoardRev1IoPoints[3].momentary, BoardProfiles::kFlowIOBoardRev1IoPoints[3].pulseMs},
    {DomainRole::Robot, PortRelay5, false, false, 0U},
    {DomainRole::Lights, PortRelay6, false, false, 0U},
    {DomainRole::FillPump, PortRelay7, false, false, 0U},
    {DomainRole::WaterHeater, PortRelay8, false, false, 0U},
};

inline constexpr const AnalogRoleDefault* analogDefaultForRole(DomainRole role)
{
    for (const AnalogRoleDefault& entry : kAnalogRoleDefaults) {
        if (entry.role == role) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalInputRoleDefault* digitalInputDefaultForRole(DomainRole role)
{
    for (const DigitalInputRoleDefault& entry : kDigitalInputRoleDefaults) {
        if (entry.role == role) return &entry;
    }
    return nullptr;
}

inline constexpr const DigitalOutputRoleDefault* digitalOutputDefaultForRole(DomainRole role)
{
    for (const DigitalOutputRoleDefault& entry : kDigitalOutputRoleDefaults) {
        if (entry.role == role) return &entry;
    }
    return nullptr;
}

}  // namespace IoLayout
}  // namespace FlowIO
}  // namespace Profiles
