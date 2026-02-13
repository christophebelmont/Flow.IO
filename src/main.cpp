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
#include "Modules/Network/TimeModule/TimeModule.h"
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
#include "Modules/PoolLogicModule/PoolLogicModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/CommandModule/CommandModule.h"

#include "Core/Layout/PoolIoMap.h"
#include "Core/Layout/PoolSensorMap.h"
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
static TimeModule           timeModule;
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
static PoolLogicModule      poolLogicModule;
static DataStore*           gIoDataStore = nullptr;

static OneWireBus oneWireWater(19);
static OneWireBus oneWireAir(18);
static TaskHandle_t ledRandomTaskHandle = nullptr;

static char topicRuntimeMux[128] = {0};
static char topicRuntimeState[128] = {0};
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

struct RuntimeMuxStats {
    uint32_t seq = 0;
    uint32_t tsMs = 0;
    uint8_t routesTotal = 0;
    uint8_t routesPublished = 0;
    uint8_t routesSkippedNoChange = 0;
    uint8_t routesSkippedMask = 0;
    uint8_t buildErrors = 0;
    uint8_t publishErrors = 0;
    uint32_t activeDirtyMask = 0;
};
static RuntimeMuxStats gRuntimeMuxStats{};

static constexpr uint8_t IO_DO_COUNT = FLOW_POOL_IO_BINDING_COUNT;

static constexpr float PH_INTERNAL_C0 = 0.9583f;
static constexpr float PH_INTERNAL_C1 = 4.834f;
static constexpr float PH_EXTERNAL_C0 = -2.50133333f;
static constexpr float PH_EXTERNAL_C1 = 6.9f;

static constexpr float ORP_INTERNAL_C0 = 129.2f;
static constexpr float ORP_INTERNAL_C1 = 384.1f;
static constexpr float ORP_EXTERNAL_C0 = 431.03f;
static constexpr float ORP_EXTERNAL_C1 = 0.0f;

static constexpr float PSI_DEFAULT_C0 = 0.377923399f;
static constexpr float PSI_DEFAULT_C1 = -0.17634473f;

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

static void onIoFloatValue(void* ctx, float value) {
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*gIoDataStore, idx, value, millis(), DIRTY_SENSORS);
}

static void onIoBoolValue(void* ctx, bool value) {
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

    RuntimeMuxStats st{};
    st.seq = gRuntimeMuxStats.seq + 1;
    st.tsMs = millis();
    st.routesTotal = gRuntimeRouteCount;
    uint32_t activeDirtyMask = mqtt->activeSensorsDirtyMask();
    if (activeDirtyMask == 0U) activeDirtyMask = (DIRTY_SENSORS | DIRTY_ACTUATORS);
    st.activeDirtyMask = activeDirtyMask;

    for (uint8_t i = 0; i < gRuntimeRouteCount; ++i) {
        RuntimeSnapshotRoute& route = gRuntimeRoutes[i];
        if (!route.provider) continue;
        if ((route.dirtyMask & activeDirtyMask) == 0U) {
            ++st.routesSkippedMask;
            continue;
        }

        uint32_t ts = 0;
        if (!route.provider->buildRuntimeSnapshot(route.snapshotIdx, out, len, ts)) {
            ++st.buildErrors;
            continue;
        }
        if (ts <= route.lastPublishedTs) {
            ++st.routesSkippedNoChange;
            continue;
        }

        if (mqtt->publish(route.topic, out, 0, false)) {
            route.lastPublishedTs = ts;
            ++st.routesPublished;
        } else {
            ++st.publishErrors;
        }
    }

    gRuntimeMuxStats = st;
    // Mux callback publishes detailed routes manually and does not publish its bound topic.
    return false;
}

static bool buildRuntimeMuxState(MQTTModule* mqtt, char* out, size_t len) {
    if (!mqtt || !out || len == 0) return false;
    const RuntimeMuxStats& st = gRuntimeMuxStats;
    int wrote = snprintf(
        out, len,
        "{\"v\":1,\"ts\":%lu,\"seq\":%lu,\"routes_total\":%u,\"routes_published\":%u,"
        "\"routes_skipped_nochange\":%u,\"routes_skipped_mask\":%u,"
        "\"build_errors\":%u,\"publish_errors\":%u,\"dirty_active_mask\":%lu}",
        (unsigned long)millis(),
        (unsigned long)st.seq,
        (unsigned)st.routesTotal,
        (unsigned)st.routesPublished,
        (unsigned)st.routesSkippedNoChange,
        (unsigned)st.routesSkippedMask,
        (unsigned)st.buildErrors,
        (unsigned)st.publishErrors,
        (unsigned long)st.activeDirtyMask
    );
    return (wrote > 0) && ((size_t)wrote < len);
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
        const IOServiceV2* io = services.get<IOServiceV2>("io");
        if (io && io->writeDigital) {
            const uint8_t idx = (uint8_t)(esp_random() % IO_DO_COUNT);
            const IoId did = FLOW_POOL_IO_BINDINGS[idx].ioId;
            (void)io->writeDigital(io->ctx, did, (uint8_t)(esp_random() & 1U), millis());
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
    moduleManager.add(&timeModule);
    moduleManager.add(&mqttModule);
    moduleManager.add(&haModule);
    moduleManager.add(&systemModule);
    moduleManager.add(&ioModule);
    moduleManager.add(&poolLogicModule);
    moduleManager.add(&poolDeviceModule);

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
    setAdcDefaultCalib(orpDef, ORP_INTERNAL_C0, ORP_INTERNAL_C1, ORP_EXTERNAL_C0, ORP_EXTERNAL_C1);
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
    setAdcDefaultCalib(phDef, PH_INTERNAL_C0, PH_INTERNAL_C1, PH_EXTERNAL_C0, PH_EXTERNAL_C1);
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
    psiDef.c0 = PSI_DEFAULT_C0;
    psiDef.c1 = PSI_DEFAULT_C1;
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
    // ADS1115 has channels 0..3. This spare uses the 4th input channel.
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
    waterDef.minValid = -55.0f;
    waterDef.maxValid = 125.0f;
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
    airDef.minValid = -55.0f;
    airDef.maxValid = 125.0f;
    airDef.onValueChanged = onIoFloatValue;
    airDef.onValueCtx = (void*)(uintptr_t)air->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(airDef), "define analog air temperature");

    IODigitalInputDefinition poolLevelDef{};
    const PoolSensorBinding* level = flowPoolSensorBySlot(POOL_SENSOR_SLOT_POOL_LEVEL);
    requireSetup(level != nullptr, "missing sensor mapping Pool Level");
    snprintf(poolLevelDef.id, sizeof(poolLevelDef.id), "%s", level->endpointId);
    poolLevelDef.ioId = level->ioId;
    poolLevelDef.pin = 34;
    poolLevelDef.activeHigh = true;
    poolLevelDef.pullMode = IO_PULL_NONE;
    poolLevelDef.onValueChanged = onIoBoolValue;
    poolLevelDef.onValueCtx = (void*)(uintptr_t)level->runtimeIndex;
    requireSetup(ioModule.defineDigitalInput(poolLevelDef), "define digital input pool level");

    static_assert(FLOW_POOL_IO_BINDING_COUNT == 8, "Unexpected pool IO binding count");

    for (uint8_t i = 0; i < FLOW_POOL_IO_BINDING_COUNT; ++i) {
        const PoolIoBinding& b = FLOW_POOL_IO_BINDINGS[i];
        const uint8_t logical = (uint8_t)(b.ioId - IO_ID_DO_BASE);

        IODigitalOutputDefinition d{};
        snprintf(d.id, sizeof(d.id), "%s", b.haObjectSuffix ? b.haObjectSuffix : "output");
        d.ioId = b.ioId;
        d.activeHigh = false;
        d.initialOn = false;

        switch (logical) {
            case 0: d.pin = 32; break; // filtration
            case 1: d.pin = 25; break; // pH pump
            case 2: d.pin = 26; break; // chlorine pump
            case 3: d.pin = 13; d.momentary = true; d.pulseMs = 500; break; // chlorine generator
            case 4: d.pin = 33; break; // robot
            case 5: d.pin = 27; break; // lights
            case 6: d.pin = 23; break; // fill pump
            case 7: d.pin = 12; break; // water heater
            default:
                requireSetup(false, "unknown digital output logical index");
                break;
        }

        requireSetup(ioModule.defineDigitalOutput(d), "define digital output");
    }

    for (uint8_t slot = 0; slot < FLOW_POOL_IO_BINDING_COUNT; ++slot) {
        const PoolIoBinding* b = flowPoolIoBindingBySlot(slot);
        requireSetup(b != nullptr, "missing pool slot mapping");

        PoolDeviceDefinition pd{};
        snprintf(pd.label, sizeof(pd.label), "%s", b->name ? b->name : "Pool Device");
        pd.ioId = b->ioId;
        pd.type = POOL_DEVICE_RELAY_STD;

        if (slot == 0) {
            pd.type = POOL_DEVICE_FILTRATION;
        } else if (slot == 1 || slot == 2) {
            pd.type = POOL_DEVICE_PERISTALTIC;
            pd.flowLPerHour = 1.2f;
            pd.tankCapacityMl = 20000.0f;
            pd.tankInitialMl = 20000.0f;
            pd.dependsOnMask = (uint8_t)(1u << 0);
        } else if (slot == 3) {
            pd.dependsOnMask = (uint8_t)(1u << 0);
        }

        requireSetup(poolDeviceModule.defineDevice(pd), "define pool device");
    }

    
    bool ok = moduleManager.initAll(registry, services);
    if (!ok) {
        while (true) delay(1000);
    }

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    gIoDataStore = dsSvc ? dsSvc->store : nullptr;

    mqttModule.formatTopic(topicRuntimeMux, sizeof(topicRuntimeMux), "rt/runtime/mux");
    mqttModule.formatTopic(topicRuntimeState, sizeof(topicRuntimeState), "rt/runtime/state");
    gRuntimeRouteCount = 0;
    (void)registerRuntimeProvider(mqttModule, &ioModule);
    (void)registerRuntimeProvider(mqttModule, &poolDeviceModule);
    mqttModule.formatTopic(topicNetworkState, sizeof(topicNetworkState), "rt/network/state");
    mqttModule.formatTopic(topicSystemState, sizeof(topicSystemState), "rt/system/state");
    mqttModule.setSensorsPublisher(topicRuntimeMux, publishRuntimeStates);
    mqttModule.addRuntimePublisher(topicRuntimeState, 30000, 0, false, buildRuntimeMuxState);
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
        "__        __   _                               _        \n"
        "\\ \\      / /__| | ___ ___  _ __ ___   ___     | |_ ___  \n"
        " \\ \\ /\\ / / _ \\ |/ __/ _ \\| '_ ` _ \\ / _ \\    | __/ _ \\ \n"
        "  \\ V  V /  __/ | (_| (_) | | | | | |  __/    | || (_) |\n"
        "   \\_/\\_/ \\___|_|\\___\\___/|_| |_| |_|\\___|     \\__\\___/ \n"
        "                                                        \n"
        "         _____ _                     ___ ___            \n"
        "        |  ___| | _____      __     |_ _/ _ \\           \n"
        " _____  | |_  | |/ _ \\ \\ /\\ / /      | | | | |  _____   \n"
        "|_____| |  _| | | (_) \\ V  V /   _   | | |_| | |_____|  \n"
        "        |_|   |_|\\___/ \\_/\\_/   (_) |___\\___/           \n"
        "\x1b[0m"
        );   
}
     

void loop() {
}
