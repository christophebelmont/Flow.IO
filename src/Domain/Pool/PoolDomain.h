#pragma once

#include "Domain/DomainSpec.h"
#include "Domain/Pool/PoolDefaults.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"

namespace PoolDomain {

inline constexpr DomainIoBinding kIoBindings[] = {
    {BoardSignal::Relay1, DomainRole::FiltrationPump},
    {BoardSignal::Relay2, DomainRole::PhPump},
    {BoardSignal::Relay3, DomainRole::ChlorinePump},
    {BoardSignal::Relay4, DomainRole::ChlorineGenerator},
    {BoardSignal::Relay5, DomainRole::Robot},
    {BoardSignal::Relay6, DomainRole::Lights},
    {BoardSignal::Relay7, DomainRole::FillPump},
    {BoardSignal::Relay8, DomainRole::WaterHeater},
    {BoardSignal::DigitalIn1, DomainRole::WaterCounterSensor},
    {BoardSignal::DigitalIn2, DomainRole::PhLevelSensor},
    {BoardSignal::DigitalIn3, DomainRole::ChlorineLevelSensor},
    {BoardSignal::DigitalIn4, DomainRole::PoolLevelSensor},
    {BoardSignal::AnalogIn1, DomainRole::OrpSensor},
    {BoardSignal::AnalogIn2, DomainRole::PhSensor},
    {BoardSignal::AnalogIn3, DomainRole::PsiSensor},
    {BoardSignal::AnalogIn4, DomainRole::SpareAnalog},
    {BoardSignal::TempProbe1, DomainRole::WaterTemp},
    {BoardSignal::TempProbe2, DomainRole::AirTemp},
};

inline constexpr PoolDevicePreset kPoolDevices[] = {
    {DomainRole::FiltrationPump, "io_flt_pmp", "Filtration Pump", "mdi:pool", 0, POOL_DEVICE_FILTRATION, 0.0f, 0.0f, 0.0f, DomainRole::None, 0},
    {DomainRole::PhPump, "io_ph_pmp", "pH Pump", "mdi:beaker-outline", 1, POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, DomainRole::FiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {DomainRole::ChlorinePump, "io_chl_pmp", "Chlorine Pump", "mdi:water-outline", 2, POOL_DEVICE_PERISTALTIC, PoolDefaults::PeristalticFlowLPerHour,
     PoolDefaults::PeristalticTankCapacityMl, PoolDefaults::PeristalticTankInitialMl, DomainRole::FiltrationPump,
     PoolDefaults::DosePumpMaxUptimeDaySec},
    {DomainRole::Robot, "io_robot", "Robot", "mdi:robot-vacuum", 3, POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, DomainRole::FiltrationPump, 0},
    {DomainRole::FillPump, "io_fill_pmp", "Fill Pump", "mdi:waves-arrow-up", 4, POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, DomainRole::None,
     PoolDefaults::FillPumpMaxUptimeDaySec},
    {DomainRole::ChlorineGenerator, "io_chl_gen", "Chlorine Generator", "mdi:flash", 5, POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     DomainRole::FiltrationPump, PoolDefaults::ChlorineGeneratorMaxUptimeDaySec},
    {DomainRole::Lights, "io_lights", "Lights", "mdi:lightbulb", 6, POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f, DomainRole::None, 0},
    {DomainRole::WaterHeater, "io_wat_htr", "Water Heater", "mdi:water-boiler", 7, POOL_DEVICE_RELAY_STD, 0.0f, 0.0f, 0.0f,
     DomainRole::None, 0},
};

inline constexpr DomainSensorPreset kSensors[] = {
    {DomainRole::OrpSensor, "ORP", "ORP", 0, 0, false, true, 0},
    {DomainRole::PhSensor, "pH", "pH", 1, 1, false, true, 0},
    {DomainRole::PsiSensor, "PSI", "PSI", 2, 2, false, true, 0},
    {DomainRole::SpareAnalog, "Spare", "Spare", 3, 3, false, true, 0},
    {DomainRole::WaterTemp, "Water Temperature", "Water Temperature", 4, 4, false, true, 0},
    {DomainRole::AirTemp, "Air Temperature", "Air Temperature", 5, 5, false, true, 0},
    {DomainRole::PoolLevelSensor, "Pool Level", "Pool Level", 6, 6, true, true, 0},
    {DomainRole::PhLevelSensor, "pH Level", "pH Level", 7, 7, true, true, 0},
    {DomainRole::ChlorineLevelSensor, "Chlorine Level", "Chlorine Level", 8, 8, true, true, 0},
    {DomainRole::WaterCounterSensor, "Water Counter", "Water Counter", 9, 9, true, true, 0},
};

inline constexpr DomainSpec kPoolDomain{
    "Pool",
    kIoBindings,
    (uint8_t)(sizeof(kIoBindings) / sizeof(kIoBindings[0])),
    kPoolDevices,
    (uint8_t)(sizeof(kPoolDevices) / sizeof(kPoolDevices[0])),
    kSensors,
    (uint8_t)(sizeof(kSensors) / sizeof(kSensors[0])),
    &PoolDefaults::kLogicDefaults,
    nullptr
};

}  // namespace PoolDomain
