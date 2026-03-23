#pragma once

#include <stdint.h>

enum class ModuleId : uint8_t {
    Unknown = 0,
    LogHub,
    LogDispatcher,
    LogSinkSerial,
    LogSinkAlarm,
    EventBus,
    ConfigStore,
    DataStore,
    Command,
    Alarm,
    Wifi,
    WifiProvisioning,
    Time,
    I2cCfgClient,
    I2cCfgServer,
    WebInterface,
    FirmwareUpdate,
    System,
    SystemMonitor,
    Ha,
    Mqtt,
    Io,
    PoolDevice,
    PoolLogic,
    Hmi,
    SupervisorHmi,
    Count
};

constexpr uint8_t kModuleIdCount = static_cast<uint8_t>(ModuleId::Count);

constexpr bool isValidModuleId(ModuleId id)
{
    return id != ModuleId::Unknown && static_cast<uint8_t>(id) < kModuleIdCount;
}

constexpr uint8_t moduleIdIndex(ModuleId id)
{
    return static_cast<uint8_t>(id);
}

constexpr const char* toString(ModuleId id)
{
    switch (id) {
        case ModuleId::LogHub: return "loghub";
        case ModuleId::LogDispatcher: return "log.dispatcher";
        case ModuleId::LogSinkSerial: return "log.sink.serial";
        case ModuleId::LogSinkAlarm: return "log.sink.alarm";
        case ModuleId::EventBus: return "eventbus";
        case ModuleId::ConfigStore: return "config";
        case ModuleId::DataStore: return "datastore";
        case ModuleId::Command: return "cmd";
        case ModuleId::Alarm: return "alarms";
        case ModuleId::Wifi: return "wifi";
        case ModuleId::WifiProvisioning: return "wifiprov";
        case ModuleId::Time: return "time";
        case ModuleId::I2cCfgClient: return "i2ccfg.client";
        case ModuleId::I2cCfgServer: return "i2ccfg.server";
        case ModuleId::WebInterface: return "webinterface";
        case ModuleId::FirmwareUpdate: return "fwupdate";
        case ModuleId::System: return "system";
        case ModuleId::SystemMonitor: return "sysmon";
        case ModuleId::Ha: return "ha";
        case ModuleId::Mqtt: return "mqtt";
        case ModuleId::Io: return "io";
        case ModuleId::PoolDevice: return "pooldev";
        case ModuleId::PoolLogic: return "poollogic";
        case ModuleId::Hmi: return "hmi";
        case ModuleId::SupervisorHmi: return "hmi.supervisor";
        case ModuleId::Unknown:
        case ModuleId::Count:
        default:
            return "unknown";
    }
}
