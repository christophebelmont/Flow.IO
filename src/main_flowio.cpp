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
#include "Core/ServiceRegistry.h"

/// Load Modules
// Network modules
#include "Modules/Network/WifiModule/WifiModule.h"
#include "Modules/Network/TimeModule/TimeModule.h"
#include "Modules/Network/MQTTModule/MQTTModule.h"
#include "Modules/Network/HAModule/HAModule.h"
#include "Modules/Network/I2CCfgServerModule/I2CCfgServerModule.h"
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
#include "Core/WokwiDefaultOverrides.h"
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED_MODULE((LogModuleId)LogModuleIdValue::Core, OUT, LEN, FMT, ##__VA_ARGS__)

/// Only necessary services (global)
#include "Core/Services/iLogger.h"

static Preferences preferences;
static ConfigStore registry;

static ModuleManager moduleManager;
static ServiceRegistry services;

///static LoggerModule loggerModule;
static WifiModule           wifiModule;
static TimeModule           timeModule;
static I2CCfgServerModule   i2cCfgServerModule;
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

static constexpr uint8_t IO_DO_COUNT = FLOW_POOL_IO_BINDING_COUNT;

static void onIoFloatValue(void* ctx, float value) {
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointFloat(*gIoDataStore, idx, value, millis());
}

static void onIoBoolValue(void* ctx, bool value) {
    if (!gIoDataStore) return;
    uint8_t idx = (uint8_t)(uintptr_t)ctx;
    setIoEndpointBool(*gIoDataStore, idx, value, millis());
}

static void requireSetup(bool ok, const char* step)
{
    if (ok) return;
    Board::SerialMap::logSerial().printf("Setup failure: %s\r\n", step ? step : "unknown");
    while (true) delay(1000);
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
    Board::SerialMap::logSerial().printf("[BOOT] staged startup armed (mqtt=%lums ha=%lums poollogic=%lums)\r\n",
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
        Board::SerialMap::logSerial().printf("[BOOT] mqtt stage released at %lums\r\n", (unsigned long)elapsed);
    }

    if (!gBootOrchestrator.haReleased && elapsed >= Limits::Boot::HaStartDelayMs) {
        haModule.setStartupReady(true);
        gBootOrchestrator.haReleased = true;
        Board::SerialMap::logSerial().printf("[BOOT] ha stage released at %lums\r\n", (unsigned long)elapsed);
    }

    if (!gBootOrchestrator.poolLogicReleased && elapsed >= Limits::Boot::PoolLogicStartDelayMs) {
        poolLogicModule.setStartupReady(true);
        gBootOrchestrator.poolLogicReleased = true;
        Board::SerialMap::logSerial().printf("[BOOT] poollogic stage released at %lums\r\n", (unsigned long)elapsed);
    }

    if (gBootOrchestrator.mqttReleased && gBootOrchestrator.haReleased && gBootOrchestrator.poolLogicReleased) {
        gBootOrchestrator.active = false;
        Board::SerialMap::logSerial().print("[BOOT] staged startup completed\r\n");
    }
}
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
    moduleManager.add(&i2cCfgServerModule);
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
    orpDef.source = FLOW_WIRDEF_IO_A0S;
    orpDef.channel = FLOW_WIRDEF_IO_A0C;
    orpDef.c0 = FLOW_WIRDEF_IO_A00;
    orpDef.c1 = FLOW_WIRDEF_IO_A01;
    orpDef.precision = FLOW_WIRDEF_IO_A0P;
    orpDef.minValid = FLOW_WIRDEF_IO_A0N;
    orpDef.maxValid = FLOW_WIRDEF_IO_A0X;
    orpDef.onValueChanged = onIoFloatValue;
    orpDef.onValueCtx = (void*)(uintptr_t)orp->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(orpDef), "define analog ORP");

    IOAnalogDefinition phDef{};
    const PoolSensorBinding* ph = flowPoolSensorBySlot(POOL_SENSOR_SLOT_PH);
    requireSetup(ph != nullptr, "missing sensor mapping pH");
    snprintf(phDef.id, sizeof(phDef.id), "%s", ph->endpointId);
    phDef.ioId = ph->ioId;
    phDef.source = FLOW_WIRDEF_IO_A1S;
    phDef.channel = FLOW_WIRDEF_IO_A1C;
    phDef.c0 = FLOW_WIRDEF_IO_A10;
    phDef.c1 = FLOW_WIRDEF_IO_A11;
    phDef.precision = FLOW_WIRDEF_IO_A1P;
    phDef.minValid = FLOW_WIRDEF_IO_A1N;
    phDef.maxValid = FLOW_WIRDEF_IO_A1X;
    phDef.onValueChanged = onIoFloatValue;
    phDef.onValueCtx = (void*)(uintptr_t)ph->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(phDef), "define analog pH");

    IOAnalogDefinition psiDef{};
    const PoolSensorBinding* psi = flowPoolSensorBySlot(POOL_SENSOR_SLOT_PSI);
    requireSetup(psi != nullptr, "missing sensor mapping PSI");
    snprintf(psiDef.id, sizeof(psiDef.id), "%s", psi->endpointId);
    psiDef.ioId = psi->ioId;
    psiDef.source = FLOW_WIRDEF_IO_A2S;
    psiDef.channel = FLOW_WIRDEF_IO_A2C;
    psiDef.c0 = FLOW_WIRDEF_IO_A20;
    psiDef.c1 = FLOW_WIRDEF_IO_A21;
    psiDef.precision = FLOW_WIRDEF_IO_A2P;
    psiDef.minValid = FLOW_WIRDEF_IO_A2N;
    psiDef.maxValid = FLOW_WIRDEF_IO_A2X;
    psiDef.onValueChanged = onIoFloatValue;
    psiDef.onValueCtx = (void*)(uintptr_t)psi->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(psiDef), "define analog PSI");

    IOAnalogDefinition spareDef{};
    const PoolSensorBinding* spare = flowPoolSensorBySlot(POOL_SENSOR_SLOT_SPARE);
    requireSetup(spare != nullptr, "missing sensor mapping Spare");
    snprintf(spareDef.id, sizeof(spareDef.id), "%s", spare->endpointId);
    spareDef.ioId = spare->ioId;
    spareDef.source = FLOW_WIRDEF_IO_A3S;
    // ADS1115 has channels 0..3. This spare uses the 4th input channel.
    spareDef.channel = FLOW_WIRDEF_IO_A3C;
    spareDef.c0 = FLOW_WIRDEF_IO_A30;
    spareDef.c1 = FLOW_WIRDEF_IO_A31;
    spareDef.precision = FLOW_WIRDEF_IO_A3P;
    spareDef.minValid = FLOW_WIRDEF_IO_A3N;
    spareDef.maxValid = FLOW_WIRDEF_IO_A3X;
    spareDef.onValueChanged = onIoFloatValue;
    spareDef.onValueCtx = (void*)(uintptr_t)spare->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(spareDef), "define analog Spare");

    IOAnalogDefinition waterDef{};
    const PoolSensorBinding* water = flowPoolSensorBySlot(POOL_SENSOR_SLOT_WATER_TEMP);
    requireSetup(water != nullptr, "missing sensor mapping Water Temperature");
    snprintf(waterDef.id, sizeof(waterDef.id), "%s", water->endpointId);
    waterDef.ioId = water->ioId;
    waterDef.source = FLOW_WIRDEF_IO_A4S;
    waterDef.channel = FLOW_WIRDEF_IO_A4C;
    waterDef.c0 = FLOW_WIRDEF_IO_A40;
    waterDef.c1 = FLOW_WIRDEF_IO_A41;
    waterDef.precision = FLOW_WIRDEF_IO_A4P;
    waterDef.minValid = FLOW_WIRDEF_IO_A4N;
    waterDef.maxValid = FLOW_WIRDEF_IO_A4X;
    waterDef.onValueChanged = onIoFloatValue;
    waterDef.onValueCtx = (void*)(uintptr_t)water->runtimeIndex;
    requireSetup(ioModule.defineAnalogInput(waterDef), "define analog water temperature");

    IOAnalogDefinition airDef{};
    const PoolSensorBinding* air = flowPoolSensorBySlot(POOL_SENSOR_SLOT_AIR_TEMP);
    requireSetup(air != nullptr, "missing sensor mapping Air Temperature");
    snprintf(airDef.id, sizeof(airDef.id), "%s", air->endpointId);
    airDef.ioId = air->ioId;
    airDef.source = FLOW_WIRDEF_IO_A5S;
    airDef.channel = FLOW_WIRDEF_IO_A5C;
    airDef.c0 = FLOW_WIRDEF_IO_A50;
    airDef.c1 = FLOW_WIRDEF_IO_A51;
    airDef.precision = FLOW_WIRDEF_IO_A5P;
    airDef.minValid = FLOW_WIRDEF_IO_A5N;
    airDef.maxValid = FLOW_WIRDEF_IO_A5X;
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

    IODigitalInputDefinition phLevelDef{};
    const PoolSensorBinding* phLevel = flowPoolSensorBySlot(POOL_SENSOR_SLOT_PH_LEVEL);
    requireSetup(phLevel != nullptr, "missing sensor mapping pH Level");
    snprintf(phLevelDef.id, sizeof(phLevelDef.id), "%s", phLevel->endpointId);
    phLevelDef.ioId = phLevel->ioId;
    phLevelDef.pin = Board::DI::PhLevel;
    phLevelDef.activeHigh = true;
    phLevelDef.pullMode = IO_PULL_NONE;
    phLevelDef.onValueChanged = onIoBoolValue;
    phLevelDef.onValueCtx = (void*)(uintptr_t)phLevel->runtimeIndex;
    requireSetup(ioModule.defineDigitalInput(phLevelDef), "define digital input pH level");

    IODigitalInputDefinition chlorineLevelDef{};
    const PoolSensorBinding* chlorineLevel = flowPoolSensorBySlot(POOL_SENSOR_SLOT_CHLORINE_LEVEL);
    requireSetup(chlorineLevel != nullptr, "missing sensor mapping Chlorine Level");
    snprintf(chlorineLevelDef.id, sizeof(chlorineLevelDef.id), "%s", chlorineLevel->endpointId);
    chlorineLevelDef.ioId = chlorineLevel->ioId;
    chlorineLevelDef.pin = Board::DI::ChlorineLevel;
    chlorineLevelDef.activeHigh = true;
    chlorineLevelDef.pullMode = IO_PULL_NONE;
    chlorineLevelDef.onValueChanged = onIoBoolValue;
    chlorineLevelDef.onValueCtx = (void*)(uintptr_t)chlorineLevel->runtimeIndex;
    requireSetup(ioModule.defineDigitalInput(chlorineLevelDef), "define digital input chlorine level");

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

    requireSetup(mqttModule.registerRuntimeProvider(&ioModule), "register runtime provider io");
    requireSetup(mqttModule.registerRuntimeProvider(&poolDeviceModule), "register runtime provider pooldev");
    requireSetup(mqttModule.registerRuntimeProvider(&poolLogicModule), "register runtime provider poollogic");

    
    bool ok = moduleManager.initAll(registry, services);
    if (!ok) {
        while (true) delay(1000);
    }

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    gIoDataStore = dsSvc ? dsSvc->store : nullptr;

    mqttModule.formatTopic(topicNetworkState, sizeof(topicNetworkState), "rt/network/state");
    mqttModule.formatTopic(topicSystemState, sizeof(topicSystemState), "rt/system/state");
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
        1);
*/
    Board::SerialMap::logSerial().print(
        "\x1b[34m"
        "__        __   _                               _        \r\n"
        "\\ \\      / /__| | ___ ___  _ __ ___   ___     | |_ ___  \r\n"
        " \\ \\ /\\ / / _ \\ |/ __/ _ \\| '_ ` _ \\ / _ \\    | __/ _ \\ \r\n"
        "  \\ V  V /  __/ | (_| (_) | | | | | |  __/    | || (_) |\r\n"
        "   \\_/\\_/ \\___|_|\\___\\___/|_| |_| |_|\\___|     \\__\\___/ \r\n"
        "                                                        \r\n"
        "         _____ _                     ___ ___            \r\n"
        "        |  ___| | _____      __     |_ _/ _ \\           \r\n"
        " _____  | |_  | |/ _ \\ \\ /\\ / /      | | | | |  _____   \r\n"
        "|_____| |  _| | | (_) \\ V  V /   _   | | |_| | |_____|  \r\n"
        "        |_|   |_|\\___/ \\_/\\_/   (_) |___\\___/           \r\n"
        "\x1b[0m\r\n"
        );   
}
     

void loop() {
    runBootOrchestrator();
    delay(20);
}
