#pragma once

#include <stdint.h>

#include "Board/BoardTypes.h"

struct AppContext;

enum class DomainRole : uint8_t {
    None = 0,
    FiltrationPump,
    PhPump,
    ChlorinePump,
    ChlorineGenerator,
    Robot,
    Lights,
    FillPump,
    WaterHeater,
    PoolLevelSensor,
    PhLevelSensor,
    ChlorineLevelSensor,
    WaterCounterSensor,
    OrpSensor,
    PhSensor,
    PsiSensor,
    SpareAnalog,
    WaterTemp,
    AirTemp,
    SupervisorDisplay,
    FlowLink
};

struct DomainIoBinding {
    BoardSignal signal;
    DomainRole role;
};

struct PoolDevicePreset {
    DomainRole role;
    const char* objectSuffix;
    const char* displayName;
    const char* haIcon;
    uint8_t legacySlot;
    uint8_t poolDeviceType;
    float flowLPerHour;
    float tankCapacityMl;
    float tankInitialMl;
    DomainRole dependsOnRole;
    int32_t maxUptimeDaySec;
};

struct DomainSensorPreset {
    DomainRole role;
    const char* endpointId;
    const char* displayName;
    uint8_t legacySlot;
    uint8_t runtimeIndex;
    bool digitalInput;
    bool activeHigh;
    uint8_t pullMode;
};

struct PoolLogicDefaultsSpec {
    float tempLow;
    float tempHigh;
    uint8_t filtrationStartMinHour;
    uint8_t filtrationStopMaxHour;
    float psiLow;
    float psiHigh;
    float winterStartTempC;
    float freezeHoldTempC;
    float secureElectroTempC;
    float phSetpoint;
    float orpSetpoint;
    float phKp;
    float phKi;
    float phKd;
    float orpKp;
    float orpKi;
    float orpKd;
    int32_t pidWindowMs;
    int32_t pidMinOnMs;
    int32_t pidSampleMs;
    uint8_t psiStartupDelaySec;
    uint8_t delayPidsMin;
    uint8_t delayElectroMin;
    uint8_t robotDelayMin;
    uint8_t robotDurationMin;
    uint8_t fillingMinOnSec;
};
