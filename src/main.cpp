/**
 * @file main.cpp
 * @brief Firmware entry point and module wiring.
 */
#include <Arduino.h>
#include <Preferences.h>    ///< Preference needs to be singleton-like global to work

/// Load Core Functions
#include "Core/LogHub.h"
#include "Core/LogSinkRegistry.h"

#include "Core/ConfigMigrations.h"
#include "Core/ConfigStore.h"
#include "Core/DataStore/DataStore.h"
#include "Core/ModuleManager.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ServiceRegistry.h"

/// Load Modules
// Network modules
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/Network/NTPModule/NTPModule.h"
#include "Modules/Network/MQTTModule/MQTTModule.h"
#include "Modules/Network/HAModule/HAModule.h"
// Stores Modules
#include "Modules/Stores/ConfigStoreModule/ConfigStoreModule.h"
#include "Modules/Stores/DataStoreModule/DataStoreModule.h"
// System Modules
#include "Modules/System/SystemModule/SystemModule.h"
#include "Modules/System/SystemMonitorModule/SystemMonitorModule.h"
// Logs Modules
#include "Modules/Logs/LogHubModule/LogHubModule.h"
#include "Modules/Logs/LogSerialSinkModule/LogSerialSinkModule.h"
#include "Modules/Logs/LogDispatcherModule/LogDispatcherModule.h"

#include "Modules/IOModule/IOModule.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/CommandModule/CommandModule.h"

#include "Modules/IOModule/IORuntime.h"
#include "Core/SystemStats.h"
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>

/// Only necessary services (global)
#include "Core/Services/iLogger.h"

static Preferences preferences;
static ConfigStore registry;

static ModuleManager moduleManager;
static ServiceRegistry services;

///static LoggerModule loggerModule;
static WifiModule           wifiModule;
static NTPModule            ntpModule;
static CommandModule        commandModule;
static ConfigStoreModule    configStoreModule;
static DataStoreModule      dataStoreModule;
static MQTTModule           mqttModule;
static HAModule             haModule;
static SystemModule         systemModule;
static SystemMonitorModule  systemMonitorModule;
static LogSerialSinkModule  logSerialSinkModule;
static LogDispatcherModule  logDispatcherModule;
static LogHubModule         logHubModule;
static EventBusModule       eventBusModule;
static IOModule             ioModule;
static PoolDeviceModule     poolDeviceModule;
static DataStore*           gIoDataStore = nullptr;

static OneWireBus oneWireWater(19);
static OneWireBus oneWireAir(18);
static TaskHandle_t ledRandomTaskHandle = nullptr;

static char topicRuntimeMux[128] = {0};
static char topicNetworkState[128] = {0};
static char topicSystemState[128] = {0};

static constexpr uint8_t MAX_RUNTIME_ROUTES = 32;
struct RuntimeSnapshotRoute {
    const IRuntimeSnapshotProvider* provider = nullptr;
    uint8_t snapshotIdx = 0;
    char topic[128] = {0};
    uint32_t dirtyMask = DIRTY_SENSORS;
    uint32_t lastPublishedTs = 0;
};
static RuntimeSnapshotRoute gRuntimeRoutes[MAX_RUNTIME_ROUTES]{};
static uint8_t gRuntimeRouteCount = 0;

static constexpr uint8_t IO_IDX_PH = 0;
static constexpr uint8_t IO_IDX_ORP = 1;
static constexpr uint8_t IO_IDX_PSI = 2;
static constexpr uint8_t IO_IDX_WATER_TEMP = 3;
static constexpr uint8_t IO_IDX_AIR_TEMP = 4;

static void onIoFloatValue(void* ctx, float value) {
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*gIoDataStore, idx, value, millis(), DIRTY_SENSORS);
}

static bool registerRuntimeProvider(MQTTModule& mqtt, const IRuntimeSnapshotProvider* provider) {
    if (!provider) return false;

    bool any = false;
    const uint8_t count = provider->runtimeSnapshotCount();
    for (uint8_t idx = 0; idx < count; ++idx) {
        if (gRuntimeRouteCount >= MAX_RUNTIME_ROUTES) break;
        const char* suffix = provider->runtimeSnapshotSuffix(idx);
        if (!suffix || suffix[0] == '\0') continue;

        RuntimeSnapshotRoute& route = gRuntimeRoutes[gRuntimeRouteCount++];
        route.provider = provider;
        route.snapshotIdx = idx;
        route.dirtyMask = (strncmp(suffix, "rt/io/output/", 13) == 0) ? DIRTY_ACTUATORS : DIRTY_SENSORS;
        route.lastPublishedTs = 0;
        mqtt.formatTopic(route.topic, sizeof(route.topic), suffix);
        any = true;
    }
    return any;
}

static bool publishRuntimeStates(MQTTModule* mqtt, char* out, size_t len) {
    if (!mqtt) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;
    gIoDataStore = ds;
    uint32_t activeDirtyMask = mqtt->activeSensorsDirtyMask();
    if (activeDirtyMask == 0U) activeDirtyMask = (DIRTY_SENSORS | DIRTY_ACTUATORS);

    for (uint8_t i = 0; i < gRuntimeRouteCount; ++i) {
        RuntimeSnapshotRoute& route = gRuntimeRoutes[i];
        if (!route.provider) continue;
        if ((route.dirtyMask & activeDirtyMask) == 0U) continue;

        uint32_t ts = 0;
        if (!route.provider->buildRuntimeSnapshot(route.snapshotIdx, out, len, ts)) continue;
        if (ts <= route.lastPublishedTs) continue;

        if (mqtt->publish(route.topic, out, 0, false)) {
            route.lastPublishedTs = ts;
        }
    }

    // Prevent publisher from posting on its bound topic; snapshots are published manually above.
    return false;
}

static bool buildNetworkSnapshot(MQTTModule* mqtt, char* out, size_t len) {
    if (!mqtt) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;

    IpV4 ip4 = wifiIp(*ds);
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4.b[0], ip4.b[1], ip4.b[2], ip4.b[3]);
    bool netReady = wifiReady(*ds);
    bool mqttOk = mqttReady(*ds);
    int rssi = (WiFi.isConnected()) ? WiFi.RSSI() : -127;

    snprintf(out, len,
             "{\"ready\":%s,\"ip\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
             netReady ? "true" : "false",
             ip,
             rssi,
             mqttOk ? "true" : "false",
             (unsigned long)millis());
    return true;
}

static bool buildSystemSnapshot(MQTTModule* mqtt, char* out, size_t len) {
    if (!mqtt) return false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    snprintf(out, len,
             "{\"upt_ms\":%lu,\"heap\":{\"free\":%lu,\"min\":%lu,\"largest\":%lu,\"frag\":%u},\"ts\":%lu}",
             (unsigned long)snap.uptimeMs,
             (unsigned long)snap.heap.freeBytes,
             (unsigned long)snap.heap.minFreeBytes,
             (unsigned long)snap.heap.largestFreeBlock,
             (unsigned int)snap.heap.fragPercent,
             (unsigned long)millis());
    return true;
}
/* Test task currently deactivated */
static void ledRandomTask(void*)
{
    for (;;) {
        const IOLedMaskService* leds = services.get<IOLedMaskService>("io_leds");
        if (leds && leds->setMask) {
            uint8_t mask = (uint8_t)(esp_random() & 0xFF);
            leds->setMask(leds->ctx, mask);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(50);
    preferences.begin("flowio", false);
    registry.setPreferences(preferences);
    registry.runMigrations(CURRENT_CFG_VERSION, steps, MIGRATION_COUNT);

    moduleManager.add(&logHubModule);
    moduleManager.add(&logDispatcherModule);
    moduleManager.add(&logSerialSinkModule);
    moduleManager.add(&eventBusModule);

    moduleManager.add(&configStoreModule);
    moduleManager.add(&dataStoreModule);
    moduleManager.add(&commandModule);
    moduleManager.add(&wifiModule);
    moduleManager.add(&ntpModule);
    moduleManager.add(&mqttModule);
    moduleManager.add(&haModule);
    moduleManager.add(&systemModule);
    moduleManager.add(&ioModule);
    moduleManager.add(&poolDeviceModule);

    systemMonitorModule.setModuleManager(&moduleManager);
    moduleManager.add(&systemMonitorModule);

    ioModule.setOneWireBuses(&oneWireWater, &oneWireAir);

    IOAnalogDefinition phDef{};
    snprintf(phDef.id, sizeof(phDef.id), "pH");
    phDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    phDef.channel = 0;
    phDef.precision = 1;
    phDef.onValueChanged = onIoFloatValue;
    phDef.onValueCtx = (void*)(uintptr_t)IO_IDX_PH;
    ioModule.defineAnalogInput(phDef);

    IOAnalogDefinition orpDef{};
    snprintf(orpDef.id, sizeof(orpDef.id), "ORP");
    orpDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    orpDef.channel = 1;
    orpDef.precision = 0;
    orpDef.onValueChanged = onIoFloatValue;
    orpDef.onValueCtx = (void*)(uintptr_t)IO_IDX_ORP;
    ioModule.defineAnalogInput(orpDef);

    IOAnalogDefinition psiDef{};
    snprintf(psiDef.id, sizeof(psiDef.id), "PSI");
    psiDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    psiDef.channel = 2;
    psiDef.precision = 1;
    psiDef.onValueChanged = onIoFloatValue;
    psiDef.onValueCtx = (void*)(uintptr_t)IO_IDX_PSI;
    ioModule.defineAnalogInput(psiDef);

    IOAnalogDefinition waterDef{};
    snprintf(waterDef.id, sizeof(waterDef.id), "Water Temperature");
    waterDef.source = IO_SRC_DS18_WATER;
    waterDef.channel = 0;
    waterDef.precision = 1;
    waterDef.minValid = -55.0f;
    waterDef.maxValid = 125.0f;
    waterDef.onValueChanged = onIoFloatValue;
    waterDef.onValueCtx = (void*)(uintptr_t)IO_IDX_WATER_TEMP;
    ioModule.defineAnalogInput(waterDef);

    IOAnalogDefinition airDef{};
    snprintf(airDef.id, sizeof(airDef.id), "Air Temperature");
    airDef.source = IO_SRC_DS18_AIR;
    airDef.channel = 0;
    airDef.precision = 1;
    airDef.minValid = -55.0f;
    airDef.maxValid = 125.0f;
    airDef.onValueChanged = onIoFloatValue;
    airDef.onValueCtx = (void*)(uintptr_t)IO_IDX_AIR_TEMP;
    ioModule.defineAnalogInput(airDef);

    IODigitalOutputDefinition d0{};
    snprintf(d0.id, sizeof(d0.id), "filtration_pump");
    d0.pin = 32;
    d0.activeHigh = false;
    d0.initialOn = false;
    ioModule.defineDigitalOutput(d0);

    IODigitalOutputDefinition d1{};
    snprintf(d1.id, sizeof(d1.id), "ph_pump");
    d1.pin = 25;
    d1.activeHigh = false;
    d1.initialOn = false;
    ioModule.defineDigitalOutput(d1);

    IODigitalOutputDefinition d2{};
    snprintf(d2.id, sizeof(d2.id), "chlorine_pump");
    d2.pin = 26;
    d2.activeHigh = false;
    d2.initialOn = false;
    ioModule.defineDigitalOutput(d2);

    IODigitalOutputDefinition d3{};
    snprintf(d3.id, sizeof(d3.id), "chlorine_generator");
    d3.pin = 13;
    d3.activeHigh = false;
    d3.initialOn = false;
    d3.momentary = true;
    d3.pulseMs = 500;
    ioModule.defineDigitalOutput(d3);

    IODigitalOutputDefinition d4{};
    snprintf(d4.id, sizeof(d4.id), "robot");
    d4.pin = 33;
    d4.activeHigh = false;
    d4.initialOn = false;
    ioModule.defineDigitalOutput(d4);

    IODigitalOutputDefinition d5{};
    snprintf(d5.id, sizeof(d5.id), "lights");
    d5.pin = 27;
    d5.activeHigh = false;
    d5.initialOn = false;
    ioModule.defineDigitalOutput(d5);

    IODigitalOutputDefinition d6{};
    snprintf(d6.id, sizeof(d6.id), "fill_pump");
    d6.pin = 23;
    d6.activeHigh = false;
    d6.initialOn = false;
    ioModule.defineDigitalOutput(d6);

    IODigitalOutputDefinition d7{};
    snprintf(d7.id, sizeof(d7.id), "water_heater");
    d7.pin = 12;
    d7.activeHigh = false;
    d7.initialOn = false;
    ioModule.defineDigitalOutput(d7);

    PoolDeviceDefinition pd0{};
    snprintf(pd0.label, sizeof(pd0.label), "Filtration Pump");
    snprintf(pd0.ioId, sizeof(pd0.ioId), "d0");
    pd0.type = POOL_DEVICE_FILTRATION;
    poolDeviceModule.defineDevice(pd0);

    PoolDeviceDefinition pd1{};
    snprintf(pd1.label, sizeof(pd1.label), "pH Pump");
    snprintf(pd1.ioId, sizeof(pd1.ioId), "d1");
    pd1.type = POOL_DEVICE_PERISTALTIC;
    pd1.flowLPerHour = 1.2f;
    pd1.tankCapacityMl = 20000.0f;
    pd1.tankInitialMl = 20000.0f;
    pd1.dependsOnMask = (uint8_t)(1u << 0);
    poolDeviceModule.defineDevice(pd1);

    PoolDeviceDefinition pd2{};
    snprintf(pd2.label, sizeof(pd2.label), "Chlorine Pump");
    snprintf(pd2.ioId, sizeof(pd2.ioId), "d2");
    pd2.type = POOL_DEVICE_PERISTALTIC;
    pd2.flowLPerHour = 1.2f;
    pd2.tankCapacityMl = 20000.0f;
    pd2.tankInitialMl = 20000.0f;
    pd2.dependsOnMask = (uint8_t)(1u << 0);
    poolDeviceModule.defineDevice(pd2);

    
    bool ok = moduleManager.initAll(registry, services);
    if (!ok) {
        while (true) delay(1000);
    }

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    gIoDataStore = dsSvc ? dsSvc->store : nullptr;

    mqttModule.formatTopic(topicRuntimeMux, sizeof(topicRuntimeMux), "rt/runtime/state");
    gRuntimeRouteCount = 0;
    (void)registerRuntimeProvider(mqttModule, &ioModule);
    (void)registerRuntimeProvider(mqttModule, &poolDeviceModule);
    mqttModule.formatTopic(topicNetworkState, sizeof(topicNetworkState), "rt/network/state");
    mqttModule.formatTopic(topicSystemState, sizeof(topicSystemState), "rt/system/state");
    mqttModule.setSensorsPublisher(topicRuntimeMux, publishRuntimeStates);
    mqttModule.addRuntimePublisher(topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    mqttModule.addRuntimePublisher(topicSystemState, 60000, 0, false, buildSystemSnapshot);

    /*xTaskCreatePinnedToCore(
        ledRandomTask,
        "led_random",
        3072,
        nullptr,
        1,
        &ledRandomTaskHandle,
        1
    );*/

    Serial.print(
        "\x1b[34m"
        " _____  _                   ___  ___          \n"
        "|  ___|| |  ___ __      __ |_ _|/ _ \\         \n"
        "| |_   | | / _ \\\\ \\ /\\ / /  | || | | |  _____ \n"
        "|  _|  | || (_) |\\ V  V /_  | || |_| | |_____|\n"
        "|_|    |_| \\___/  \\_/\\_/(_)|___|\\___/         \n"
        "\x1b[0m"
    );

}

void loop() {
}
