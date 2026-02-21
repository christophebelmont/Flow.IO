#pragma once
/**
 * @file ConfigBranchIds.h
 * @brief Stable config branch identifiers used for cfg/* routing.
 */

#include <stdint.h>

enum class ConfigBranchId : uint16_t {
    Unknown = 0,

    Wifi = 1,
    Mqtt = 2,
    Ha = 3,
    Time = 4,
    TimeScheduler = 5,
    SystemMonitor = 6,
    PoolLogic = 7,
    Alarms = 8,

    PoolLogicMode = 9,
    PoolLogicFiltration = 10,
    PoolLogicSensors = 11,
    PoolLogicPid = 12,
    PoolLogicDelay = 13,
    PoolLogicDevice = 14,

    Io = 16,
    IoDebug = 17,
    I2cCfgServer = 18,
    I2cCfgClient = 19,
    IoInputA0 = 32,
    IoInputA1 = 33,
    IoInputA2 = 34,
    IoInputA3 = 35,
    IoInputA4 = 36,
    IoInputA5 = 37,
    IoOutputD0 = 48,
    IoOutputD1 = 49,
    IoOutputD2 = 50,
    IoOutputD3 = 51,
    IoOutputD4 = 52,
    IoOutputD5 = 53,
    IoOutputD6 = 54,
    IoOutputD7 = 55,

    PoolDevicePd0 = 64,
    PoolDevicePd1 = 65,
    PoolDevicePd2 = 66,
    PoolDevicePd3 = 67,
    PoolDevicePd4 = 68,
    PoolDevicePd5 = 69,
    PoolDevicePd6 = 70,
    PoolDevicePd7 = 71,

    PoolDeviceRuntimePd0 = 80,
    PoolDeviceRuntimePd1 = 81,
    PoolDeviceRuntimePd2 = 82,
    PoolDeviceRuntimePd3 = 83,
    PoolDeviceRuntimePd4 = 84,
    PoolDeviceRuntimePd5 = 85,
    PoolDeviceRuntimePd6 = 86,
    PoolDeviceRuntimePd7 = 87
};

constexpr ConfigBranchId configBranchFromPoolDeviceSlot(uint8_t slot)
{
    if (slot > 7U) return ConfigBranchId::Unknown;
    return static_cast<ConfigBranchId>((uint16_t)ConfigBranchId::PoolDevicePd0 + slot);
}

constexpr ConfigBranchId configBranchFromPoolDeviceRuntimeSlot(uint8_t slot)
{
    if (slot > 7U) return ConfigBranchId::Unknown;
    return static_cast<ConfigBranchId>((uint16_t)ConfigBranchId::PoolDeviceRuntimePd0 + slot);
}

inline const char* configBranchModuleName(ConfigBranchId id)
{
    switch (id) {
        case ConfigBranchId::Wifi: return "wifi";
        case ConfigBranchId::Mqtt: return "mqtt";
        case ConfigBranchId::Ha: return "ha";
        case ConfigBranchId::Time: return "time";
        case ConfigBranchId::TimeScheduler: return "time/scheduler";
        case ConfigBranchId::SystemMonitor: return "sysmon";
        case ConfigBranchId::PoolLogic: return "poollogic";
        case ConfigBranchId::Alarms: return "alarms";
        case ConfigBranchId::PoolLogicMode: return "poollogic/mode";
        case ConfigBranchId::PoolLogicFiltration: return "poollogic/filtration";
        case ConfigBranchId::PoolLogicSensors: return "poollogic/sensors";
        case ConfigBranchId::PoolLogicPid: return "poollogic/pid";
        case ConfigBranchId::PoolLogicDelay: return "poollogic/delay";
        case ConfigBranchId::PoolLogicDevice: return "poollogic/device";
        case ConfigBranchId::Io: return "io";
        case ConfigBranchId::IoDebug: return "io/debug";
        case ConfigBranchId::I2cCfgServer: return "i2c/cfg/server";
        case ConfigBranchId::I2cCfgClient: return "i2c/cfg/client";
        case ConfigBranchId::IoInputA0: return "io/input/a0";
        case ConfigBranchId::IoInputA1: return "io/input/a1";
        case ConfigBranchId::IoInputA2: return "io/input/a2";
        case ConfigBranchId::IoInputA3: return "io/input/a3";
        case ConfigBranchId::IoInputA4: return "io/input/a4";
        case ConfigBranchId::IoInputA5: return "io/input/a5";
        case ConfigBranchId::IoOutputD0: return "io/output/d0";
        case ConfigBranchId::IoOutputD1: return "io/output/d1";
        case ConfigBranchId::IoOutputD2: return "io/output/d2";
        case ConfigBranchId::IoOutputD3: return "io/output/d3";
        case ConfigBranchId::IoOutputD4: return "io/output/d4";
        case ConfigBranchId::IoOutputD5: return "io/output/d5";
        case ConfigBranchId::IoOutputD6: return "io/output/d6";
        case ConfigBranchId::IoOutputD7: return "io/output/d7";
        case ConfigBranchId::PoolDevicePd0: return "pdm/pd0";
        case ConfigBranchId::PoolDevicePd1: return "pdm/pd1";
        case ConfigBranchId::PoolDevicePd2: return "pdm/pd2";
        case ConfigBranchId::PoolDevicePd3: return "pdm/pd3";
        case ConfigBranchId::PoolDevicePd4: return "pdm/pd4";
        case ConfigBranchId::PoolDevicePd5: return "pdm/pd5";
        case ConfigBranchId::PoolDevicePd6: return "pdm/pd6";
        case ConfigBranchId::PoolDevicePd7: return "pdm/pd7";
        case ConfigBranchId::PoolDeviceRuntimePd0: return "pdmrt/pd0";
        case ConfigBranchId::PoolDeviceRuntimePd1: return "pdmrt/pd1";
        case ConfigBranchId::PoolDeviceRuntimePd2: return "pdmrt/pd2";
        case ConfigBranchId::PoolDeviceRuntimePd3: return "pdmrt/pd3";
        case ConfigBranchId::PoolDeviceRuntimePd4: return "pdmrt/pd4";
        case ConfigBranchId::PoolDeviceRuntimePd5: return "pdmrt/pd5";
        case ConfigBranchId::PoolDeviceRuntimePd6: return "pdmrt/pd6";
        case ConfigBranchId::PoolDeviceRuntimePd7: return "pdmrt/pd7";
        case ConfigBranchId::Unknown:
        default:
            return nullptr;
    }
}
