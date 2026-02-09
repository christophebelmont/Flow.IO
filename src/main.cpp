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
#include "Core/ServiceRegistry.h"

/// Load Modules
// Network modules
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/Network/NTPModule/NTPModule.h"
#include "Modules/Network/MQTTModule/MQTTModule.h"
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
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/CommandModule/CommandModule.h"

#include "Modules/IOModule/IORuntime.h"
#include "Core/SystemStats.h"
#include <WiFi.h>
#include <esp_system.h>

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
static SystemModule         systemModule;
static SystemMonitorModule  systemMonitorModule;
static LogSerialSinkModule  logSerialSinkModule;
static LogDispatcherModule  logDispatcherModule;
static LogHubModule         logHubModule;
static EventBusModule       eventBusModule;
static IOModule             ioModule;
static DataStore*           gIoDataStore = nullptr;

static OneWireBus oneWireWater(19);
static OneWireBus oneWireAir(18);
static TaskHandle_t ledRandomTaskHandle = nullptr;

static char topicIoInputState[128] = {0};
static char topicIoOutputState[128] = {0};
static char topicNetworkState[128] = {0};
static char topicSystemState[128] = {0};

static constexpr uint8_t IO_IDX_PH = 0;
static constexpr uint8_t IO_IDX_ORP = 1;
static constexpr uint8_t IO_IDX_PSI = 2;
static constexpr uint8_t IO_IDX_WATER_TEMP = 3;
static constexpr uint8_t IO_IDX_AIR_TEMP = 4;
static uint32_t gIoInputLastPublishedTs = 0;
static uint32_t gIoOutputLastPublishedTs = 0;

static void onIoFloatValue(void* ctx, float value) {
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*gIoDataStore, idx, value, millis(), DIRTY_SENSORS);
}

static bool isInputEndpointId(const char* id)
{
    return id && id[0] == 'a' && id[1] >= '0' && id[1] <= '9' && id[2] == '\0';
}

static bool isOutputEndpointId(const char* id)
{
    if (!id || id[0] == '\0') return false;
    if (id[0] == 'd' && id[1] >= '0' && id[1] <= '9' && id[2] == '\0') return true;
    return strcmp(id, "status_leds_mask") == 0;
}

static bool buildIoGroupSnapshot(char* out, size_t len, bool inputGroup, uint32_t& maxTsOut)
{
    size_t used = 0;
    int wrote = snprintf(out, len, "{");
    if (wrote < 0 || (size_t)wrote >= len) return false;
    used += (size_t)wrote;

    IORegistry& reg = ioModule.registry();
    bool first = true;
    uint32_t maxTs = 0;
    for (uint8_t i = 0; i < reg.count(); ++i) {
        IOEndpoint* ep = reg.at(i);
        if (!ep) continue;
        if ((ep->capabilities() & IO_CAP_READ) == 0) continue;
        const char* id = ep->id();
        if (!id || id[0] == '\0') continue;
        if (inputGroup && !isInputEndpointId(id)) continue;
        if (!inputGroup && !isOutputEndpointId(id)) continue;

        IOEndpointValue v{};
        bool ok = ep->read(v);
        if (!ok) v.valid = false;

        const char* label = ioModule.endpointLabel(id);
        wrote = snprintf(out + used, len - used, "%s\"%s\":{\"name\":\"%s\",\"value\":",
                         first ? "" : ",",
                         id,
                         (label && label[0] != '\0') ? label : id);
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;
        first = false;

        if (!v.valid) {
            wrote = snprintf(out + used, len - used, "null");
        } else if (v.valueType == IO_EP_VALUE_BOOL) {
            wrote = snprintf(out + used, len - used, "%s", v.v.b ? "true" : "false");
        } else if (v.valueType == IO_EP_VALUE_FLOAT) {
            wrote = snprintf(out + used, len - used, "%.3f", (double)v.v.f);
        } else if (v.valueType == IO_EP_VALUE_INT32) {
            wrote = snprintf(out + used, len - used, "%ld", (long)v.v.i);
        } else {
            wrote = snprintf(out + used, len - used, "null");
        }
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        wrote = snprintf(out + used, len - used, "}");
        if (wrote < 0 || (size_t)wrote >= (len - used)) return false;
        used += (size_t)wrote;

        if (v.timestampMs > maxTs) maxTs = v.timestampMs;
    }

    wrote = snprintf(out + used, len - used, ",\"ts\":%lu}", (unsigned long)millis());
    if (wrote < 0 || (size_t)wrote >= (len - used)) return false;

    maxTsOut = maxTs;
    return true;
}

static bool publishIoStates(MQTTModule* mqtt, char* out, size_t len) {
    if (!mqtt) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;
    (void)ds;

    bool published = false;
    uint32_t inputTs = 0;
    if (buildIoGroupSnapshot(out, len, true, inputTs)) {
        if (inputTs > gIoInputLastPublishedTs) {
            if (mqtt->publish(topicIoInputState, out, 0, false)) {
                gIoInputLastPublishedTs = inputTs;
                published = true;
            }
        }
    }

    uint32_t outputTs = 0;
    if (buildIoGroupSnapshot(out, len, false, outputTs)) {
        if (outputTs > gIoOutputLastPublishedTs) {
            if (mqtt->publish(topicIoOutputState, out, 0, false)) {
                gIoOutputLastPublishedTs = outputTs;
                published = true;
            }
        }
    }

    // Prevent publisher from posting on its bound topic; we publish manually above.
    (void)published;
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
    moduleManager.add(&systemModule);
    moduleManager.add(&ioModule);

    systemMonitorModule.setModuleManager(&moduleManager);
    moduleManager.add(&systemMonitorModule);

    ioModule.setOneWireBuses(&oneWireWater, &oneWireAir);

    IOAnalogDefinition phDef{};
    snprintf(phDef.id, sizeof(phDef.id), "ph");
    phDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    phDef.channel = 0;
    phDef.precision = 1;
    phDef.onValueChanged = onIoFloatValue;
    phDef.onValueCtx = (void*)(uintptr_t)IO_IDX_PH;
    ioModule.defineAnalogInput(phDef);

    IOAnalogDefinition orpDef{};
    snprintf(orpDef.id, sizeof(orpDef.id), "orp");
    orpDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    orpDef.channel = 1;
    orpDef.precision = 0;
    orpDef.onValueChanged = onIoFloatValue;
    orpDef.onValueCtx = (void*)(uintptr_t)IO_IDX_ORP;
    ioModule.defineAnalogInput(orpDef);

    IOAnalogDefinition psiDef{};
    snprintf(psiDef.id, sizeof(psiDef.id), "psi");
    psiDef.source = IO_SRC_ADS_INTERNAL_SINGLE;
    psiDef.channel = 2;
    psiDef.precision = 1;
    psiDef.onValueChanged = onIoFloatValue;
    psiDef.onValueCtx = (void*)(uintptr_t)IO_IDX_PSI;
    ioModule.defineAnalogInput(psiDef);

    IOAnalogDefinition waterDef{};
    snprintf(waterDef.id, sizeof(waterDef.id), "water_temp");
    waterDef.source = IO_SRC_DS18_WATER;
    waterDef.channel = 0;
    waterDef.precision = 1;
    waterDef.minValid = -55.0f;
    waterDef.maxValid = 125.0f;
    waterDef.onValueChanged = onIoFloatValue;
    waterDef.onValueCtx = (void*)(uintptr_t)IO_IDX_WATER_TEMP;
    ioModule.defineAnalogInput(waterDef);

    IOAnalogDefinition airDef{};
    snprintf(airDef.id, sizeof(airDef.id), "air_temp");
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

    
    bool ok = moduleManager.initAll(registry, services);
    if (!ok) {
        while (true) delay(1000);
    }

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    gIoDataStore = dsSvc ? dsSvc->store : nullptr;

    mqttModule.formatTopic(topicIoInputState, sizeof(topicIoInputState), "rt/io/input/state");
    mqttModule.formatTopic(topicIoOutputState, sizeof(topicIoOutputState), "rt/io/output/state");
    mqttModule.formatTopic(topicNetworkState, sizeof(topicNetworkState), "rt/network/state");
    mqttModule.formatTopic(topicSystemState, sizeof(topicSystemState), "rt/system/state");
    mqttModule.setSensorsPublisher(topicIoInputState, publishIoStates);
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
