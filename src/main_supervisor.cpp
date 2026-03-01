/**
 * @file main_supervisor.cpp
 * @brief Supervisor firmware entry point and module wiring.
 */
#include <Arduino.h>
#include <Preferences.h>

#include "Core/NvsKeys.h"
#include "Core/ConfigMigrations.h"
#include "Core/ConfigStore.h"
#include "Core/ModuleManager.h"
#include "Core/ServiceRegistry.h"

#include "Modules/Logs/LogHubModule/LogHubModule.h"
#include "Modules/Logs/LogDispatcherModule/LogDispatcherModule.h"
#include "Modules/Logs/LogSerialSinkModule/LogSerialSinkModule.h"
#include "Modules/Logs/LogAlarmSinkModule/LogAlarmSinkModule.h"

#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/Stores/ConfigStoreModule/ConfigStoreModule.h"
#include "Modules/Stores/DataStoreModule/DataStoreModule.h"
#include "Modules/CommandModule/CommandModule.h"
#include "Modules/AlarmModule/AlarmModule.h"
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/Network/WifiProvisioningModule/WifiProvisioningModule.h"
#include "Modules/Network/TimeModule/TimeModule.h"
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientModule.h"
#include "Modules/Network/WebInterfaceModule/WebInterfaceModule.h"
#include "Modules/Network/FirmwareUpdateModule/FirmwareUpdateModule.h"
#include "Modules/System/SystemModule/SystemModule.h"
#include "Modules/System/SystemMonitorModule/SystemMonitorModule.h"

static Preferences preferences;
static ConfigStore registry;

static ModuleManager moduleManager;
static ServiceRegistry services;

static LogHubModule logHubModule;
static LogDispatcherModule logDispatcherModule;
static LogSerialSinkModule logSerialSinkModule;
static EventBusModule eventBusModule;
static ConfigStoreModule configStoreModule;
static DataStoreModule dataStoreModule;
static CommandModule commandModule;
static AlarmModule alarmModule;
static LogAlarmSinkModule logAlarmSinkModule;
static WifiModule wifiModule;
static WifiProvisioningModule wifiProvisioningModule;
static TimeModule timeModule;
static I2CCfgClientModule i2cCfgClientModule;
static WebInterfaceModule webInterfaceModule;
static FirmwareUpdateModule firmwareUpdateModule;
static SystemModule systemModule;
static SystemMonitorModule systemMonitorModule;

void setup()
{
    Serial.begin(115200);
    delay(50);

    preferences.begin(NvsKeys::StorageNamespace, false);
    registry.setPreferences(preferences);
    registry.runMigrations(CURRENT_CFG_VERSION, steps, MIGRATION_COUNT);

    moduleManager.add(&logHubModule);
    moduleManager.add(&logDispatcherModule);
    moduleManager.add(&logSerialSinkModule);
    moduleManager.add(&eventBusModule);

    moduleManager.add(&configStoreModule);
    moduleManager.add(&dataStoreModule);
    moduleManager.add(&commandModule);
    moduleManager.add(&alarmModule);
    moduleManager.add(&logAlarmSinkModule);
    moduleManager.add(&wifiModule);
    moduleManager.add(&wifiProvisioningModule);
    moduleManager.add(&timeModule);
    moduleManager.add(&i2cCfgClientModule);
    moduleManager.add(&webInterfaceModule);
    moduleManager.add(&firmwareUpdateModule);
    moduleManager.add(&systemModule);

    systemMonitorModule.setModuleManager(&moduleManager);
    moduleManager.add(&systemMonitorModule);

    bool ok = moduleManager.initAll(registry, services);
    if (!ok) {
        while (true) delay(1000);
    }

    Serial.println("Flow.IO Supervisor profile booted");
}

void loop()
{
    delay(20);
}
