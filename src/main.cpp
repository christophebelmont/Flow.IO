/**
 * @file main.cpp
 * @brief Firmware entry point and module wiring.
 */
#include <Arduino.h>
#include <Preferences.h>
#include "Core/NvsKeys.h"    ///< Preference needs to be singleton-like global to work

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
#include "Modules/Logs/LogAlarmSinkModule/LogAlarmSinkModule.h"

#include "Modules/IOModule/IOModule.h"
#include "Modules/IOModule/IOBus/OneWireBus.h"
#include "Modules/PoolDeviceModule/PoolDeviceModule.h"
#include "Modules/PoolLogicModule/PoolLogicModule.h"
#include "Modules/EventBusModule/EventBusModule.h"
#include "Modules/CommandModule/CommandModule.h"
#include "Modules/AlarmModule/AlarmModule.h"
#include "Modules/HMIModule/HMIModule.h"

#include "Core/Layout/PoolIoMap.h"
#include "Core/Layout/PoolSensorMap.h"
#include "Modules/IOModule/IORuntime.h"
#include "Core/SystemStats.h"
#include "Core/SnprintfCheck.h"
#include "Board/BoardLayout.h"
#include "Board/BoardPinMap.h"
#include "Board/BoardSerialMap.h"
#include "Core/SystemLimits.h"
#include "Domain/Calibration.h"
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED("Main", OUT, LEN, FMT, ##__VA_ARGS__)

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
static LogAlarmSinkModule   logAlarmSinkModule;
static LogDispatcherModule  logDispatcherModule;
static LogHubModule         logHubModule;
static EventBusModule       eventBusModule;
static AlarmModule          alarmModule;
static HMIModule            hmiModule;
static IOModule             ioModule;
static PoolDeviceModule     poolDeviceModule;
static PoolLogicModule      poolLogicModule;
static DataStore*           gIoDataStore = nullptr;

static OneWireBus oneWireWater(Board::OneWire::BusA);
static OneWireBus oneWireAir(Board::OneWire::BusB);
static TaskHandle_t ledRandomTaskHandle = nullptr;

static char topicRuntimeMux[Limits::TopicBuf] = {0};
static char topicRuntimeState[Limits::TopicBuf] = {0};
static char topicNetworkState[Limits::TopicBuf] = {0};
static char topicSystemState[Limits::TopicBuf] = {0};

struct BootOrchestratorState {
    bool active = false;
    bool mqttReleased = false;
    bool haReleased = false;
    bool poolLogicReleased = false;
    uint32_t t0Ms = 0;
};
static BootOrchestratorState gBootOrchestrator{};

struct RuntimeSnapshotRoute {
    const IRuntimeSnapshotProvider* provider = nullptr;
    uint8_t snapshotIdx = 0;
    char topic[Limits::TopicBuf] = {0};
    uint32_t dirtyMask = DIRTY_SENSORS;
    uint32_t lastPublishedTs = 0;
    bool startupForce = false;
    bool startupPublished = false;
};
static RuntimeSnapshotRoute gRuntimeRoutes[Limits::MaxRuntimeRoutes]{};
static uint8_t gRuntimeRouteCount = 0;
static_assert(Limits::MaxRuntimeRoutes >=
                  (FLOW_POOL_SENSOR_BINDING_COUNT + FLOW_POOL_IO_BINDING_COUNT + (FLOW_POOL_IO_BINDING_COUNT * 2U) + 2U),
              "MaxRuntimeRoutes too small for default Flow.IO runtime providers (io + pooldev + poollogic)");

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
    Board::SerialMap::logSerial().printf("Setup failure: %s\n", step ? step : "unknown");
    while (true) delay(1000);
}

static bool registerRuntimeProvider(MQTTModule& mqtt, const IRuntimeSnapshotProvider* provider) {
    if (!provider) return false;

    bool any = false;
    const uint8_t count = provider->runtimeSnapshotCount();
    for (uint8_t idx = 0; idx < count; ++idx) {
        if (gRuntimeRouteCount >= Limits::MaxRuntimeRoutes) {
            Board::SerialMap::logSerial().printf("Runtime route limit reached (%u), provider routes truncated\n",
                                                 (unsigned)Limits::MaxRuntimeRoutes);
            break;
        }
        const char* suffix = provider->runtimeSnapshotSuffix(idx);
        if (!suffix || suffix[0] == '\0') continue;

        RuntimeSnapshotRoute& route = gRuntimeRoutes[gRuntimeRouteCount++];
        route.provider = provider;
        route.snapshotIdx = idx;
        if (strncmp(suffix, "rt/io/output/", 13) == 0) {
            route.dirtyMask = DIRTY_ACTUATORS;
        } else if (strncmp(suffix, "rt/pdm/state/", 13) == 0) {
            route.dirtyMask = DIRTY_ACTUATORS;
        } else if (strncmp(suffix, "rt/pdm/metrics/", 15) == 0) {
            route.dirtyMask = DIRTY_SENSORS;
        } else {
            route.dirtyMask = DIRTY_SENSORS;
        }
        route.startupForce =
            (strncmp(suffix, "rt/io/output/", 13) == 0) ||
            (strncmp(suffix, "rt/pdm/state/", 13) == 0);
        route.lastPublishedTs = 0;
        route.startupPublished = false;
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
        const bool forceStartupRoute = route.startupForce && !route.startupPublished;
        if (!forceStartupRoute && (route.dirtyMask & activeDirtyMask) == 0U) {
            ++st.routesSkippedMask;
            continue;
        }

        uint32_t ts = 0;
        if (!route.provider->buildRuntimeSnapshot(route.snapshotIdx, out, len, ts)) {
            ++st.buildErrors;
            continue;
        }
        if (!forceStartupRoute && ts <= route.lastPublishedTs) {
            ++st.routesSkippedNoChange;
            continue;
        }

        if (mqtt->publish(route.topic, out, 0, false)) {
            route.lastPublishedTs = ts;
            if (route.startupForce) route.startupPublished = true;
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
        "{\"v\":1,\"ts\":%lu,\"seq\":%lu,\"routes_total\":%u,\"routes_pub\":%u,"
        "\"routes_skip_nc\":%u,\"routes_skip_m\":%u,"
        "\"build_errors\":%u,\"publish_errors\":%u,\"dirty_act_m\":%lu}",
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
    if (!mqtt || !out || len == 0) return false;
    DataStore* ds = mqtt->dataStorePtr();
    if (!ds) return false;

    IpV4 ip4 = wifiIp(*ds);
    char ip[16];
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", ip4.b[0], ip4.b[1], ip4.b[2], ip4.b[3]);
    bool netReady = wifiReady(*ds);
    bool mqttOk = mqttReady(*ds);
    int rssi = (WiFi.isConnected()) ? WiFi.RSSI() : -127;

    int wrote = snprintf(out, len,
                         "{\"ready\":%s,\"ip\":\"%s\",\"rssi\":%d,\"mqtt\":%s,\"ts\":%lu}",
                         netReady ? "true" : "false",
                         ip,
                         rssi,
                         mqttOk ? "true" : "false",
                         (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

static bool buildSystemSnapshot(MQTTModule* mqtt, char* out, size_t len) {
    if (!mqtt || !out || len == 0) return false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    DataStore* ds = mqtt->dataStorePtr();
    const uint32_t rxDrop = ds ? mqttRxDrop(*ds) : 0U;
    const uint32_t parseFail = ds ? mqttParseFail(*ds) : 0U;
    const uint32_t handlerFail = ds ? mqttHandlerFail(*ds) : 0U;
    const uint32_t oversizeDrop = ds ? mqttOversizeDrop(*ds) : 0U;

    int wrote = snprintf(
        out, len,
        "{\"upt_ms\":%llu,\"upt_s\":%llu,\"heap\":{\"free\":%lu,\"min\":%lu,\"largest\":%lu,\"frag\":%u},"
        "\"mqtt_rx\":{\"rx_drop\":%lu,\"oversize_drop\":%lu,\"parse_fail\":%lu,\"handler_fail\":%lu},\"ts\":%lu}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long long)(snap.uptimeMs64 / 1000ULL),
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent,
        (unsigned long)rxDrop,
        (unsigned long)oversizeDrop,
        (unsigned long)parseFail,
        (unsigned long)handlerFail,
        (unsigned long)millis()
    );
    return (wrote > 0) && ((size_t)wrote < len);
}

static void startBootOrchestrator()
{
    gBootOrchestrator.active = true;
    gBootOrchestrator.mqttReleased = false;
    gBootOrchestrator.haReleased = false;
    gBootOrchestrator.poolLogicReleased = false;
    gBootOrchestrator.t0Ms = millis();

    // Stage gates: keep local control active immediately, delay network-heavy phases.
    mqttModule.setStartupReady(false);
    haModule.setStartupReady(false);
    poolLogicModule.setStartupReady(false);
    Board::SerialMap::logSerial().printf("[BOOT] staged startup armed (mqtt=%lums ha=%lums poollogic=%lums)\n",
                                         (unsigned long)Limits::Boot::MqttStartDelayMs,
                                         (unsigned long)Limits::Boot::HaStartDelayMs,
                                         (unsigned long)Limits::Boot::PoolLogicStartDelayMs);
}

static void runBootOrchestrator()
{
    if (!gBootOrchestrator.active) return;

    const uint32_t now = millis();
    const uint32_t elapsed = now - gBootOrchestrator.t0Ms;

    if (!gBootOrchestrator.mqttReleased && elapsed >= Limits::Boot::MqttStartDelayMs) {
        mqttModule.setStartupReady(true);
        gBootOrchestrator.mqttReleased = true;
        Board::SerialMap::logSerial().printf("[BOOT] mqtt stage released at %lums\n", (unsigned long)elapsed);
    }

    if (!gBootOrchestrator.haReleased && elapsed >= Limits::Boot::HaStartDelayMs) {
        haModule.setStartupReady(true);
        gBootOrchestrator.haReleased = true;
        Board::SerialMap::logSerial().printf("[BOOT] ha stage released at %lums\n", (unsigned long)elapsed);
    }

    if (!gBootOrchestrator.poolLogicReleased && elapsed >= Limits::Boot::PoolLogicStartDelayMs) {
        poolLogicModule.setStartupReady(true);
        gBootOrchestrator.poolLogicReleased = true;
        Board::SerialMap::logSerial().printf("[BOOT] poollogic stage released at %lums\n", (unsigned long)elapsed);
    }

    if (gBootOrchestrator.mqttReleased && gBootOrchestrator.haReleased && gBootOrchestrator.poolLogicReleased) {
        gBootOrchestrator.active = false;
        Board::SerialMap::logSerial().println("[BOOT] staged startup completed");
    }
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
    Serial.begin(Board::SerialMap::uart0Baud());
    delay(50);
    preferences.begin(NvsKeys::StorageNamespace, false);
    registry.setPreferences(preferences);
    registry.runMigrations(CURRENT_CFG_VERSION, steps, MIGRATION_COUNT);
    mqttModule.setStartupReady(false);
    haModule.setStartupReady(false);
    poolLogicModule.setStartupReady(false);

    moduleManager.add(&logHubModule);
    moduleManager.add(&logDispatcherModule);
    moduleManager.add(&logSerialSinkModule);
    moduleManager.add(&eventBusModule);

    moduleManager.add(&configStoreModule);
    moduleManager.add(&dataStoreModule);
    moduleManager.add(&commandModule);
    moduleManager.add(&hmiModule);
    moduleManager.add(&alarmModule);
    moduleManager.add(&logAlarmSinkModule);
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
    setAdcDefaultCalib(orpDef, Calib::Orp::InternalC0, Calib::Orp::InternalC1, Calib::Orp::ExternalC0, Calib::Orp::ExternalC1);
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
    setAdcDefaultCalib(phDef, Calib::Ph::InternalC0, Calib::Ph::InternalC1, Calib::Ph::ExternalC0, Calib::Ph::ExternalC1);
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

    static_assert(FLOW_POOL_IO_BINDING_COUNT == BoardLayout::DigitalOutCount, "Unexpected pool IO binding count");

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

    for (uint8_t slot = 0; slot < FLOW_POOL_IO_BINDING_COUNT; ++slot) {
        const PoolIoBinding* b = flowPoolIoBindingBySlot(slot);
        requireSetup(b != nullptr, "missing pool slot mapping");

        PoolDeviceDefinition pd{};
        snprintf(pd.label, sizeof(pd.label), "%s", b->name ? b->name : "Pool Device");
        pd.ioId = b->ioId;
        pd.type = POOL_DEVICE_RELAY_STD;

        if (slot == POOL_IO_SLOT_FILTRATION_PUMP) {
            pd.type = POOL_DEVICE_FILTRATION;
        } else if (slot == POOL_IO_SLOT_PH_PUMP || slot == POOL_IO_SLOT_CHLORINE_PUMP) {
            pd.type = POOL_DEVICE_PERISTALTIC;
            pd.flowLPerHour = 1.2f;
            pd.tankCapacityMl = 20000.0f;
            pd.tankInitialMl = 20000.0f;
            pd.dependsOnMask = (uint8_t)(1u << POOL_IO_SLOT_FILTRATION_PUMP);
            pd.maxUptimeDaySec = 30 * 60;
        } else if (slot == POOL_IO_SLOT_CHLORINE_GENERATOR || slot == POOL_IO_SLOT_ROBOT) {
            pd.dependsOnMask = (uint8_t)(1u << POOL_IO_SLOT_FILTRATION_PUMP);
            if (slot == POOL_IO_SLOT_CHLORINE_GENERATOR) {
                pd.maxUptimeDaySec = 600 * 60;
            }
        } else if (slot == POOL_IO_SLOT_FILL_PUMP) {
            pd.maxUptimeDaySec = 30 * 60;
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
    (void)registerRuntimeProvider(mqttModule, &poolLogicModule);
    mqttModule.formatTopic(topicNetworkState, sizeof(topicNetworkState), "rt/network/state");
    mqttModule.formatTopic(topicSystemState, sizeof(topicSystemState), "rt/system/state");
    mqttModule.setSensorsPublisher(topicRuntimeMux, publishRuntimeStates);
    mqttModule.addRuntimePublisher(topicRuntimeState, 30000, 0, false, buildRuntimeMuxState);
    mqttModule.addRuntimePublisher(topicNetworkState, 60000, 0, false, buildNetworkSnapshot);
    mqttModule.addRuntimePublisher(topicSystemState, 60000, 0, false, buildSystemSnapshot);
    startBootOrchestrator();

    /*xTaskCreatePinnedToCore(
        ledRandomTask,
        "led_random",
        3072,
        nullptr,
        1,
        &ledRandomTaskHandle,
        1
    );*/

    Board::SerialMap::logSerial().print(
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
    runBootOrchestrator();
    delay(20);
}
