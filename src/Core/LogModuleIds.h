#pragma once
/**
 * @file LogModuleIds.h
 * @brief Stable log module identifiers and helpers.
 */

#include "Core/ModuleId.h"
#include <stdint.h>

using LogModuleId = uint16_t;

enum class LogModuleIdValue : LogModuleId {
    Unknown = 0,
    Core = 1,

    LogHub = 2,
    LogDispatcher = 3,
    LogSinkSerial = 4,
    LogSinkAlarm = 5,

    EventBusModule = 6,
    ConfigStoreModule = 7,
    DataStoreModule = 8,
    CommandModule = 9,
    AlarmModule = 10,
    WifiModule = 11,
    WifiProvisioningModule = 12,
    TimeModule = 13,
    I2cCfgClientModule = 14,
    I2cCfgServerModule = 15,
    WebInterfaceModule = 16,
    FirmwareUpdateModule = 17,
    SystemModule = 18,
    SystemMonitorModule = 19,
    HAModule = 20,
    MQTTModule = 21,
    IOModule = 22,
    PoolDeviceModule = 23,
    PoolLogicModule = 24,
    HMIModule = 25,

    CoreI2cLink = 40,
    CoreModuleManager = 41,
    CoreConfigStore = 42,
    CoreEventBus = 43
};

static inline LogModuleId logModuleIdFromModuleId(ModuleId moduleId)
{
    switch (moduleId) {
        case ModuleId::LogHub: return (LogModuleId)LogModuleIdValue::LogHub;
        case ModuleId::LogDispatcher: return (LogModuleId)LogModuleIdValue::LogDispatcher;
        case ModuleId::LogSinkSerial: return (LogModuleId)LogModuleIdValue::LogSinkSerial;
        case ModuleId::LogSinkAlarm: return (LogModuleId)LogModuleIdValue::LogSinkAlarm;
        case ModuleId::EventBus: return (LogModuleId)LogModuleIdValue::EventBusModule;
        case ModuleId::ConfigStore: return (LogModuleId)LogModuleIdValue::ConfigStoreModule;
        case ModuleId::DataStore: return (LogModuleId)LogModuleIdValue::DataStoreModule;
        case ModuleId::Command: return (LogModuleId)LogModuleIdValue::CommandModule;
        case ModuleId::Alarm: return (LogModuleId)LogModuleIdValue::AlarmModule;
        case ModuleId::Wifi: return (LogModuleId)LogModuleIdValue::WifiModule;
        case ModuleId::WifiProvisioning: return (LogModuleId)LogModuleIdValue::WifiProvisioningModule;
        case ModuleId::Time: return (LogModuleId)LogModuleIdValue::TimeModule;
        case ModuleId::I2cCfgClient: return (LogModuleId)LogModuleIdValue::I2cCfgClientModule;
        case ModuleId::I2cCfgServer: return (LogModuleId)LogModuleIdValue::I2cCfgServerModule;
        case ModuleId::WebInterface: return (LogModuleId)LogModuleIdValue::WebInterfaceModule;
        case ModuleId::FirmwareUpdate: return (LogModuleId)LogModuleIdValue::FirmwareUpdateModule;
        case ModuleId::System: return (LogModuleId)LogModuleIdValue::SystemModule;
        case ModuleId::SystemMonitor: return (LogModuleId)LogModuleIdValue::SystemMonitorModule;
        case ModuleId::Ha: return (LogModuleId)LogModuleIdValue::HAModule;
        case ModuleId::Mqtt: return (LogModuleId)LogModuleIdValue::MQTTModule;
        case ModuleId::Io: return (LogModuleId)LogModuleIdValue::IOModule;
        case ModuleId::PoolDevice: return (LogModuleId)LogModuleIdValue::PoolDeviceModule;
        case ModuleId::PoolLogic: return (LogModuleId)LogModuleIdValue::PoolLogicModule;
        case ModuleId::Hmi:
        case ModuleId::SupervisorHmi:
            return (LogModuleId)LogModuleIdValue::HMIModule;
        case ModuleId::Unknown:
        case ModuleId::Count:
        default:
            return (LogModuleId)LogModuleIdValue::Unknown;
    }
}

static inline const char* logModuleNameFromId(LogModuleId moduleId)
{
    switch ((LogModuleIdValue)moduleId) {
        case LogModuleIdValue::Core: return "core";
        case LogModuleIdValue::LogHub: return "loghub";
        case LogModuleIdValue::LogDispatcher: return "log.dispatcher";
        case LogModuleIdValue::LogSinkSerial: return "log.sink.serial";
        case LogModuleIdValue::LogSinkAlarm: return "log.sink.alarm";
        case LogModuleIdValue::EventBusModule: return "eventbus";
        case LogModuleIdValue::ConfigStoreModule: return "config";
        case LogModuleIdValue::DataStoreModule: return "datastore";
        case LogModuleIdValue::CommandModule: return "cmd";
        case LogModuleIdValue::AlarmModule: return "alarms";
        case LogModuleIdValue::WifiModule: return "wifi";
        case LogModuleIdValue::WifiProvisioningModule: return "wifiprov";
        case LogModuleIdValue::TimeModule: return "time";
        case LogModuleIdValue::I2cCfgClientModule: return "i2ccfg.client";
        case LogModuleIdValue::I2cCfgServerModule: return "i2ccfg.server";
        case LogModuleIdValue::WebInterfaceModule: return "webinterface";
        case LogModuleIdValue::FirmwareUpdateModule: return "fwupdate";
        case LogModuleIdValue::SystemModule: return "system";
        case LogModuleIdValue::SystemMonitorModule: return "sysmon";
        case LogModuleIdValue::HAModule: return "ha";
        case LogModuleIdValue::MQTTModule: return "mqtt";
        case LogModuleIdValue::IOModule: return "io";
        case LogModuleIdValue::PoolDeviceModule: return "pooldev";
        case LogModuleIdValue::PoolLogicModule: return "poollogic";
        case LogModuleIdValue::HMIModule: return "hmi";
        case LogModuleIdValue::CoreI2cLink: return "core.i2clink";
        case LogModuleIdValue::CoreModuleManager: return "core.modulemanager";
        case LogModuleIdValue::CoreConfigStore: return "core.configstore";
        case LogModuleIdValue::CoreEventBus: return "core.eventbus";
        case LogModuleIdValue::Unknown:
        default:
            return nullptr;
    }
}
