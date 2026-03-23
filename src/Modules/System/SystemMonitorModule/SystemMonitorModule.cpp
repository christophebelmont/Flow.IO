/**
 * @file SystemMonitorModule.cpp
 * @brief Implementation file.
 */
#include "SystemMonitorModule.h"
#include "Core/BufferUsageTracker.h"
#include "Core/ModuleManager.h"   ///< required for iteration
#include "Core/Services/ILogger.h"
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

    wifiSvc = services.get<WifiService>(ServiceId::Wifi);
    cfgSvc  = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    logHub  = services.get<LogHubService>(ServiceId::LogHub);
    haSvc_  = services.get<HAService>(ServiceId::Ha);
}

void SystemMonitorModule::registerHaEntities_(ServiceRegistry& services)
{
    if (haEntitiesRegistered_) return;
    if (!haSvc_) haSvc_ = services.get<HAService>(ServiceId::Ha);
    if (!haSvc_ || !haSvc_->addSensor) return;

    const HASensorEntry uptimeSeconds{
        "system",
        "sys_upt_s",
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
        "sys_hp_free",
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
        "sys_hp_min_free",
        "Heap Min Free",
        "rt/system/state",
        "{{ ((value_json.heap.min_free | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko",
        false,
        nullptr
    };
    const HASensorEntry heapFragPercent{
        "system",
        "sys_hp_frag",
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

    if (!logHub || !logHub->getStats) {
        LOGD("Heap free=%lu min_free=%lu largest=%lu frag=%u%%",
             (unsigned long)snap.heap.freeBytes,
             (unsigned long)snap.heap.minFreeBytes,
             (unsigned long)snap.heap.largestFreeBlock,
             (unsigned int)snap.heap.fragPercent);
        return;
    }

    LogHubStatsSnapshot stats{};
    logHub->getStats(logHub->ctx, &stats);
    LOGD("Heap free=%lu min_free=%lu largest=%lu frag=%u%% LogQ=%u/%u drop=%lu trunc=%lu",
         (unsigned long)snap.heap.freeBytes,
         (unsigned long)snap.heap.minFreeBytes,
         (unsigned long)snap.heap.largestFreeBlock,
         (unsigned int)snap.heap.fragPercent,
         (unsigned)stats.peakQueued,
         (unsigned)stats.queueLen,
         (unsigned long)stats.droppedCount,
         (unsigned long)stats.formatTruncCount);

    if (stats.droppedCount > 0 || stats.formatTruncCount > 0) {
        const char* lastDropModule =
            (logHub->resolveModuleName && stats.lastDropModuleId != (LogModuleId)LogModuleIdValue::Unknown)
                ? logHub->resolveModuleName(logHub->ctx, stats.lastDropModuleId)
                : "-";
        const char* lastTruncModule =
            (logHub->resolveModuleName && stats.lastFormatTruncModuleId != (LogModuleId)LogModuleIdValue::Unknown)
                ? logHub->resolveModuleName(logHub->ctx, stats.lastFormatTruncModuleId)
                : "-";
        LOGD("LogQ src drop=%s trunc=%s now=%u",
             lastDropModule ? lastDropModule : "-",
             lastTruncModule ? lastTruncModule : "-",
             (unsigned)stats.queuedNow);
    }
}

void SystemMonitorModule::logTaskStacks() {
    if (!moduleManager) {
        LOGD("ModuleManager not set, task stats disabled");
        return;
    }

    static constexpr char kStackPrefix[] = "Stack ";
    static constexpr uint8_t kMaxTasksPerLine = 3;
    static constexpr size_t kLineMsgBudget = (size_t)LOG_MSG_MAX - sizeof(kStackPrefix);
    char line[kLineMsgBudget + 1];
    size_t off = 0;
    line[0] = '\0';
    uint8_t tasksOnLine = 0;
    bool hasTask = false;

    const uint8_t n = moduleManager->getTaskEntryCount();
    for (uint8_t i = 0; i < n; ++i) {
        const ModuleManager::TaskEntry* task = moduleManager->getTaskEntry(i);
        if (!task || !task->module || !task->handle) continue;

        const ModuleTaskSpec* specs = task->module->taskSpecs();
        const uint8_t taskCount = task->module->taskCount();
        if (!specs || task->taskIndex >= taskCount) continue;
        const ModuleTaskSpec& spec = specs[task->taskIndex];

        UBaseType_t hw = uxTaskGetStackHighWaterMark(task->handle);
        hasTask = true;
        const bool isLow = (hw < 300);

        char entry[80];
        const int ew = snprintf(entry, sizeof(entry), "%s/%s@c%ld=%u%s",
                                toString(task->module->moduleId()),
                                spec.name ? spec.name : "?",
                                (long)spec.coreId,
                                (unsigned)hw,
                                isLow ? "!" : "");
        if (ew < 0) continue;

        const size_t entryLen = (size_t)ew;
        const size_t sepLen = (tasksOnLine > 0) ? 1U : 0U;

        if (tasksOnLine >= kMaxTasksPerLine || (off + sepLen + entryLen) > kLineMsgBudget) {
            if (tasksOnLine > 0) {
                LOGD("Stack %s", line);
            }
            off = 0;
            line[0] = '\0';
            tasksOnLine = 0;
        }

        if ((off + sepLen + entryLen) > kLineMsgBudget) {
            continue;
        }

        if (sepLen) {
            line[off++] = ' ';
            line[off] = '\0';
        }
        memcpy(line + off, entry, entryLen + 1);
        off += entryLen;
        ++tasksOnLine;

        if (tasksOnLine >= kMaxTasksPerLine) {
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

void SystemMonitorModule::logTrackedBuffers()
{
    TrackedBufferSnapshot snapshots[(size_t)TrackedBufferId::Count]{};
    const size_t count = BufferUsageTracker::snapshot(snapshots, (size_t)TrackedBufferId::Count);
    static constexpr char kBufPrefix[] = "Buf ";
    static constexpr size_t kLineMsgBudget = (size_t)LOG_MSG_MAX - sizeof(kBufPrefix);
    char line[kLineMsgBudget + 1];
    size_t off = 0;
    uint8_t itemsOnLine = 0;
    bool loggedAny = false;
    line[0] = '\0';

    for (size_t i = 0; i < count; ++i) {
        const TrackedBufferSnapshot& s = snapshots[i];
        if (!s.name) continue;
        if (s.peakUsed == 0U && s.capacity == 0U) continue;

        char entry[96];
        const bool isFull = (s.capacity > 0U) && (s.peakUsed >= s.capacity);
        const int ew = s.source[0]
            ? snprintf(entry, sizeof(entry), "%s=%lu/%lu%s@%s",
                       s.name,
                       (unsigned long)s.peakUsed,
                       (unsigned long)s.capacity,
                       isFull ? "!" : "",
                       s.source)
            : snprintf(entry, sizeof(entry), "%s=%lu/%lu%s",
                       s.name,
                       (unsigned long)s.peakUsed,
                       (unsigned long)s.capacity,
                       isFull ? "!" : "");
        if (ew <= 0) continue;

        const size_t entryLen = (size_t)ew;
        const size_t sepLen = (itemsOnLine > 0U) ? 1U : 0U;
        if (itemsOnLine > 0U && (off + sepLen + entryLen) > kLineMsgBudget) {
            LOGD("Buf %s", line);
            line[0] = '\0';
            off = 0;
            itemsOnLine = 0;
        }
        if ((off + ((itemsOnLine > 0U) ? 1U : 0U) + entryLen) > kLineMsgBudget) {
            if (entryLen > kLineMsgBudget) continue;
        }
        if (itemsOnLine > 0U) {
            line[off++] = ' ';
            line[off] = '\0';
        }
        memcpy(line + off, entry, entryLen + 1U);
        off += entryLen;
        ++itemsOnLine;
        loggedAny = true;
    }

    if (!loggedAny) {
        LOGD("Buf none");
        return;
    }

    if (itemsOnLine > 0U) {
        LOGD("Buf %s", line);
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
                "\"min_free\":%lu,"
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

    const uint32_t periodMs = (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : 5000U;
    const uint32_t heapOffsetMs = periodMs / 3U;
    const uint32_t bufOffsetMs = (periodMs * 2U) / 3U;

    if (traceCycleStartMs_ == 0U) {
        traceCycleStartMs_ = now;
        stackLoggedThisCycle_ = false;
        heapLoggedThisCycle_ = false;
        buffersLoggedThisCycle_ = false;
    }

    while ((uint32_t)(now - traceCycleStartMs_) >= periodMs) {
        traceCycleStartMs_ += periodMs;
        stackLoggedThisCycle_ = false;
        heapLoggedThisCycle_ = false;
        buffersLoggedThisCycle_ = false;
    }

    const uint32_t cycleElapsedMs = (uint32_t)(now - traceCycleStartMs_);
    if (!stackLoggedThisCycle_) {
        logTaskStacks();
        stackLoggedThisCycle_ = true;
    }
    if (!heapLoggedThisCycle_ && cycleElapsedMs >= heapOffsetMs) {
        logHeapStats();
        heapLoggedThisCycle_ = true;
    }
    if (!buffersLoggedThisCycle_ && cycleElapsedMs >= bufOffsetMs) {
        logTrackedBuffers();
        buffersLoggedThisCycle_ = true;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
}
