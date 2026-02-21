/**
 * @file main_supervisor.cpp
 * @brief Supervisor firmware entry point and module wiring.
 */
#include <Arduino.h>
#include <Preferences.h>

#include "Core/NvsKeys.h"
#include "Core/ConfigMigrations.h"
#include "Core/ConfigStore.h"
#include "Core/DataStore/DataStore.h"
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
#include "Modules/IOModule/IOModule.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/IOModule/IORuntime.h"

#include "Core/Layout/PoolIoMap.h"
#include "Core/Layout/PoolSensorMap.h"
#include "Board/BoardLayout.h"
#include "Board/BoardPinMap.h"
#include "Domain/Calibration.h"

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
static IOModule ioModule;
static SystemMonitorModule systemMonitorModule;

static DataStore* gIoDataStore = nullptr;

static OneWireBus oneWireWater(Board::OneWire::BusA);
static OneWireBus oneWireAir(Board::OneWire::BusB);

static void setAdcDefaultCalib(IOAnalogDefinition& def,
                               float internalC0,
                               float internalC1,
                               float externalC0,
                               float externalC1)
{
    if (def.source == IO_SRC_ADS_EXTERNAL_DIFF) {
        def.c0 = externalC0;
        def.c1 = externalC1;
    } else {
        def.c0 = internalC0;
        def.c1 = internalC1;
    }
}

static void onIoFloatValue(void* ctx, float value)
{
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*gIoDataStore, idx, value, millis(), DIRTY_SENSORS);
}

static void onIoBoolValue(void* ctx, bool value)
{
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointBool(*gIoDataStore, idx, value, millis(), DIRTY_SENSORS);
}

static void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Serial.printf("Setup failure: %s\n", step ? step : "unknown");
    while (true) delay(1000);
}

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
    // Temporarily disabled in Supervisor profile to avoid runtime conflicts.
    // moduleManager.add(&ioModule);

    systemMonitorModule.setModuleManager(&moduleManager);
    moduleManager.add(&systemMonitorModule);

    ioModule.setOneWireBuses(&oneWireWater, &oneWireAir);

    IOAnalogDefinition orpDef{};
    const PoolSensorBinding* orp = flowPoolSensorBySlot(POOL_SENSOR_SLOT_ORP);
    requireSetup(orp != nullptr, "missing sensor mapping ORP");
    snprintf(orpDef.id, sizeof(orpDef.id), "%s", orp->endpointId);
    orpDef.ioId = orp->ioId;
    orpDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    orpDef.channel = 0;
    setAdcDefaultCalib(orpDef, Calib::Orp::InternalC0, Calib::Orp::InternalC1, Calib::Orp::ExternalC0,
                       Calib::Orp::ExternalC1);
    orpDef.precision = 0;
    orpDef.onValueChanged = onIoFloatValue;
    orpDef.onValueCtx = (void*)(uintptr_t)orp->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(orpDef), "define analog ORP");

    IOAnalogDefinition phDef{};
    const PoolSensorBinding* ph = flowPoolSensorBySlot(POOL_SENSOR_SLOT_PH);
    requireSetup(ph != nullptr, "missing sensor mapping pH");
    snprintf(phDef.id, sizeof(phDef.id), "%s", ph->endpointId);
    phDef.ioId = ph->ioId;
    phDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    phDef.channel = 1;
    setAdcDefaultCalib(phDef, Calib::Ph::InternalC0, Calib::Ph::InternalC1, Calib::Ph::ExternalC0,
                       Calib::Ph::ExternalC1);
    phDef.precision = 1;
    phDef.onValueChanged = onIoFloatValue;
    phDef.onValueCtx = (void*)(uintptr_t)ph->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(phDef), "define analog pH");

    IOAnalogDefinition psiDef{};
    const PoolSensorBinding* psi = flowPoolSensorBySlot(POOL_SENSOR_SLOT_PSI);
    requireSetup(psi != nullptr, "missing sensor mapping PSI");
    snprintf(psiDef.id, sizeof(psiDef.id), "%s", psi->endpointId);
    psiDef.ioId = psi->ioId;
    psiDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    psiDef.channel = 2;
    psiDef.c0 = Calib::Psi::DefaultC0;
    psiDef.c1 = Calib::Psi::DefaultC1;
    psiDef.precision = 1;
    psiDef.onValueChanged = onIoFloatValue;
    psiDef.onValueCtx = (void*)(uintptr_t)psi->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(psiDef), "define analog PSI");

    IOAnalogDefinition spareDef{};
    const PoolSensorBinding* spare = flowPoolSensorBySlot(POOL_SENSOR_SLOT_SPARE);
    requireSetup(spare != nullptr, "missing sensor mapping Spare");
    snprintf(spareDef.id, sizeof(spareDef.id), "%s", spare->endpointId);
    spareDef.ioId = spare->ioId;
    spareDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    spareDef.channel = 3;
    spareDef.c0 = 1.0f;
    spareDef.c1 = 0.0f;
    spareDef.precision = 3;
    spareDef.onValueChanged = onIoFloatValue;
    spareDef.onValueCtx = (void*)(uintptr_t)spare->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(spareDef), "define analog Spare");

    IOAnalogDefinition waterDef{};
    const PoolSensorBinding* water = flowPoolSensorBySlot(POOL_SENSOR_SLOT_WATER_TEMP);
    requireSetup(water != nullptr, "missing sensor mapping Water Temperature");
    snprintf(waterDef.id, sizeof(waterDef.id), "%s", water->endpointId);
    waterDef.ioId = water->ioId;
    waterDef.source = IO_SRC_DS18_WATER;
    waterDef.channel = 0;
    waterDef.precision = 1;
    waterDef.minValid = Calib::Temperature::Ds18MinValidC;
    waterDef.maxValid = Calib::Temperature::Ds18MaxValidC;
    waterDef.onValueChanged = onIoFloatValue;
    waterDef.onValueCtx = (void*)(uintptr_t)water->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(waterDef), "define analog water temperature");

    IOAnalogDefinition airDef{};
    const PoolSensorBinding* air = flowPoolSensorBySlot(POOL_SENSOR_SLOT_AIR_TEMP);
    requireSetup(air != nullptr, "missing sensor mapping Air Temperature");
    snprintf(airDef.id, sizeof(airDef.id), "%s", air->endpointId);
    airDef.ioId = air->ioId;
    airDef.source = IO_SRC_DS18_AIR;
    airDef.channel = 0;
    airDef.precision = 1;
    airDef.minValid = Calib::Temperature::Ds18MinValidC;
    airDef.maxValid = Calib::Temperature::Ds18MaxValidC;
    airDef.onValueChanged = onIoFloatValue;
    airDef.onValueCtx = (void*)(uintptr_t)air->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(airDef), "define analog air temperature");

    IODigitalInputDefinition poolLevelDef{};
    const PoolSensorBinding* level = flowPoolSensorBySlot(POOL_SENSOR_SLOT_POOL_LEVEL);
    requireSetup(level != nullptr, "missing sensor mapping Pool Level");
    snprintf(poolLevelDef.id, sizeof(poolLevelDef.id), "%s", level->endpointId);
    poolLevelDef.ioId = level->ioId;
    poolLevelDef.pin = Board::DI::FlowSwitch;
    poolLevelDef.activeHigh = true;
    poolLevelDef.pullMode = IO_PULL_NONE;
    poolLevelDef.onValueChanged = onIoBoolValue;
    poolLevelDef.onValueCtx = (void*)(uintptr_t)level->runtimeIndex;
    requireSetup(ioModule.defineDigitalInput(poolLevelDef), "define digital input pool level");

    static_assert(FLOW_POOL_IO_BINDING_COUNT == BoardLayout::DigitalOutCount,
                  "Unexpected pool IO binding count");

    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        const PoolIoBinding& b = FLOW_POOL_IO_BINDINGS[i];
        const uint8_t logical = (uint8_t)(b.ioId - IO_ID_DO_BASE);

        IODigitalOutputDefinition d{};
        snprintf(d.id, sizeof(d.id), "%s", b.haObjectSuffix ? b.haObjectSuffix : "output");
        d.ioId = b.ioId;
        d.activeHigh = false;
        d.initialOn = false;

        if (logical >= BoardLayout::DigitalOutCount) {
            requireSetup(false, "unknown digital output logical index");
        }
        const DigitalOutDef& outDef = BoardLayout::DOs[logical];
        d.pin = outDef.pin;
        d.momentary = outDef.momentary;
        d.pulseMs = outDef.momentary ? outDef.pulseMs : 0;

        requireSetup(ioModule.defineDigitalOutput(d), "define digital output");
    }

    bool ok = moduleManager.initAll(registry, services);
    if (!ok) {
        while (true) delay(1000);
    }

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    gIoDataStore = dsSvc ? dsSvc->store : nullptr;

    Serial.println("Flow.IO Supervisor profile booted");
}

void loop()
{
    delay(20);
}
