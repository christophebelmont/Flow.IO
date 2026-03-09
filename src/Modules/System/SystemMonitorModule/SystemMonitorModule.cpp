/**
 * @file SystemMonitorModule.cpp
 * @brief Implementation file.
 */
#include "SystemMonitorModule.h"
#include "Core/ModuleManager.h"   ///< required for iteration
#include <Arduino.h>
#include <WiFi.h>                ///< only for RSSI (optional)
#include <new>
#include <string.h>
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::SystemMonitorModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr uint8_t kSysMonCfgProducerId = 45;
static constexpr uint8_t kSysMonCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kSysMonCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::SystemMonitor, kSysMonCfgBranch}, "sysmon", "sysmon", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
}

const char* SystemMonitorModule::wifiStateStr(WifiState st) {
    switch (st) {
    case WifiState::Disabled:    return "Disabled";
    case WifiState::Idle:        return "Idle";
    case WifiState::Connecting:  return "Connecting";
    case WifiState::Connected:   return "Connected";
    case WifiState::ErrorWait:   return "ErrorWait";
    default:                     return "Unknown";
    }
}

void SystemMonitorModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::SystemMonitor;
    constexpr uint8_t kCfgBranchId = kSysMonCfgBranch;
    cfgStore_ = &cfg;
    cfg.registerVar(tracePeriodVar_, kCfgModuleId, kCfgBranchId);

    wifiSvc = services.get<WifiService>("wifi");
    cfgSvc  = services.get<ConfigStoreService>("config");
    logHub  = services.get<LogHubService>("loghub");
    haSvc_  = services.get<HAService>("ha");
}

void SystemMonitorModule::registerHaEntities_(ServiceRegistry& services)
{
    if (haEntitiesRegistered_) return;
    if (!haSvc_) haSvc_ = services.get<HAService>("ha");
    if (!haSvc_ || !haSvc_->addSensor) return;

    const HASensorEntry uptimeSeconds{
        "system",
        "uptime_seconds",
        "Uptime",
        "rt/system/state",
        "{{ value_json.upt_s | int(0) }}",
        "diagnostic",
        "mdi:timer-outline",
        "s",
        false,
        nullptr
    };
    const HASensorEntry heapFreeBytes{
        "system",
        "heap_free_bytes",
        "Heap Free",
        "rt/system/state",
        "{{ ((value_json.heap.free | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko",
        false,
        nullptr
    };
    const HASensorEntry heapMinFreeBytes{
        "system",
        "heap_min_free_bytes",
        "Heap Min Free",
        "rt/system/state",
        "{{ ((value_json.heap.min | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko",
        false,
        nullptr
    };
    const HASensorEntry heapFragPercent{
        "system",
        "heap_fragmentation",
        "Heap Fragmentation",
        "rt/system/state",
        "{{ value_json.heap.frag | int(0) }}",
        "diagnostic",
        "mdi:chart-donut",
        "%",
        false,
        nullptr
    };

    bool ok = true;
    ok = haSvc_->addSensor(haSvc_->ctx, &uptimeSeconds) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &heapFreeBytes) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &heapMinFreeBytes) && ok;
    ok = haSvc_->addSensor(haSvc_->ctx, &heapFragPercent) && ok;
    if (ok) {
        haEntitiesRegistered_ = true;
    } else {
        LOGW("HA registration failed: system monitor entities");
    }
}

void SystemMonitorModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kSysMonCfgProducerId,
                               kSysMonCfgRoutes,
                               (uint8_t)(sizeof(kSysMonCfgRoutes) / sizeof(kSysMonCfgRoutes[0])),
                               services);
    }
    registerHaEntities_(services);
}

void SystemMonitorModule::logBootInfo() {
    LOGI("Reset reason=%s", SystemStats::resetReasonStr());
    LOGI("CPU=%luMHz", (unsigned long)ESP.getCpuFreqMHz());
    const uint32_t psramSizeBytes = ESP.getPsramSize();
    LOGI("PSRAM present=%s size=%luKB free=%luKB",
         (psramSizeBytes > 0U) ? "yes" : "no",
         (unsigned long)(psramSizeBytes / 1024U),
         (unsigned long)(ESP.getFreePsram() / 1024U));
}

void SystemMonitorModule::logHeapStats() {
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    LOGD("Heap free=%lu min=%lu largest=%lu frag=%u%%",
         (unsigned long)snap.heap.freeBytes,
         (unsigned long)snap.heap.minFreeBytes,
         (unsigned long)snap.heap.largestFreeBlock,
         (unsigned int)snap.heap.fragPercent);
}

void SystemMonitorModule::logTaskStacks() {
    if (!moduleManager) {
        LOGD("ModuleManager not set, task stats disabled");
        return;
    }

    static constexpr uint8_t kTasksPerLine = 6;
    char line[384];
    size_t off = 0;
    line[0] = '\0';
    uint8_t tasksOnLine = 0;
    bool hasTask = false;

    const uint8_t n = moduleManager->getCount();
    for (uint8_t i = 0; i < n; ++i) {
        Module* m = moduleManager->getModule(i);
        if (!m) continue;

        TaskHandle_t h = m->getTaskHandle();
        if (!h) continue;

        UBaseType_t hw = uxTaskGetStackHighWaterMark(h);
        hasTask = true;
        const bool isLow = (hw < 300);

        char entry[80];
        const int ew = snprintf(entry, sizeof(entry), "%s@c%ld=%u%s",
                                m->moduleId(),
                                (long)m->taskCore(),
                                (unsigned)hw,
                                isLow ? "!" : "");
        if (ew < 0) continue;

        const size_t entryLen = (size_t)ew;
        const size_t sepLen = (tasksOnLine > 0) ? 1U : 0U;

        if (tasksOnLine >= kTasksPerLine || (off + sepLen + entryLen) >= sizeof(line)) {
            if (tasksOnLine > 0) {
                LOGD("Stack %s", line);
            }
            off = 0;
            line[0] = '\0';
            tasksOnLine = 0;
        }

        if ((off + sepLen + entryLen) >= sizeof(line)) {
            continue;
        }

        if (sepLen) {
            line[off++] = ' ';
            line[off] = '\0';
        }
        memcpy(line + off, entry, entryLen + 1);
        off += entryLen;
        ++tasksOnLine;

        if (tasksOnLine >= kTasksPerLine) {
            LOGD("Stack %s", line);
            off = 0;
            line[0] = '\0';
            tasksOnLine = 0;
        }
    }

    if (!hasTask) {
        LOGD("Stack none");
        return;
    }

    if (tasksOnLine > 0) {
        LOGD("Stack %s", line);
    }
}

void SystemMonitorModule::buildHealthJson(char* out, size_t outLen) {
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    /// wifi
    WifiState wst = WifiState::Disabled;
    bool wcon = false;
    char ip[16] = "";
    int rssi = -127;

    if (wifiSvc) {
        wst = wifiSvc->state(wifiSvc->ctx);
        wcon = wifiSvc->isConnected(wifiSvc->ctx);
        wifiSvc->getIP(wifiSvc->ctx, ip, sizeof(ip));
        if (wcon) rssi = WiFi.RSSI();
    }

    snprintf(out, outLen,
        "{"
            "\"upt_ms\":%llu,"
            "\"heap\":{"
                "\"free\":%lu,"
                "\"min\":%lu,"
                "\"largest\":%lu,"
                "\"frag\":%u"
            "}"
        "}",
        (unsigned long long)snap.uptimeMs64,
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned int)snap.heap.fragPercent
    );
}

void SystemMonitorModule::loop() {
    if (!bootInfoLogged_) {
        bootInfoLogged_ = true;
        logBootInfo();
    }

    const uint32_t now = millis();
    if (cfgStore_) {
        cfgStore_->logNvsWriteSummaryIfDue(now, 60000U);
    }

    uint32_t periodMs = (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : 5000U;

    if (lastTraceLogMs == 0U || (uint32_t)(now - lastTraceLogMs) >= periodMs) {
        lastTraceLogMs = now;
        logTaskStacks();
        logHeapStats();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
}
