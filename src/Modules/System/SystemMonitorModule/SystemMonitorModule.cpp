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
#include <esp_heap_caps.h>
#include <new>
#include <string.h>
#ifdef CONFIG_HEAP_TASK_TRACKING
#include <esp_heap_task_info.h>
#endif
#ifndef FLOW_WEB_HEAP_FORENSICS
#define FLOW_WEB_HEAP_FORENSICS 0
#endif
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::SystemMonitorModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr uint8_t kSysMonCfgProducerId = 45;
static constexpr uint8_t kSysMonCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kSysMonCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::SystemMonitor, kSysMonCfgBranch}, "sysmon", "sysmon", (uint8_t)MqttPublishPriority::Normal, nullptr},
};

volatile bool gHeapAllocFailedPending = false;
volatile size_t gHeapAllocFailedSize = 0;
volatile uint32_t gHeapAllocFailedCaps = 0;
const char* volatile gHeapAllocFailedFunction = nullptr;

void onHeapAllocFailed_(size_t size, uint32_t caps, const char* functionName)
{
    gHeapAllocFailedSize = size;
    gHeapAllocFailedCaps = caps;
    gHeapAllocFailedFunction = functionName;
    gHeapAllocFailedPending = true;
}

#ifdef CONFIG_HEAP_TASK_TRACKING
void copyTaskName_(char* out, size_t outLen, TaskHandle_t task)
{
    if (!out || outLen == 0U) return;
    out[0] = '\0';

    const char* name = task ? pcTaskGetName(task) : pcTaskGetName(nullptr);
    if (!name || !name[0]) {
        snprintf(out, outLen, "%s", "-");
        return;
    }

    snprintf(out, outLen, "%s", name);
}
#endif

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

#if FLOW_WEB_HEAP_FORENSICS
    const esp_err_t allocHookErr = heap_caps_register_failed_alloc_callback(&onHeapAllocFailed_);
    if (allocHookErr != ESP_OK) {
        LOGW("Heap alloc-fail hook registration failed err=%d", (int)allocHookErr);
    }
#endif
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

void SystemMonitorModule::appendHeapWatchSample_(const SystemStatsSnapshot& snap)
{
    HeapWatchSample& sample = heapWatchSamples_[heapWatchWriteIndex_];
    sample.uptimeMs = snap.uptimeMs;
    sample.freeBytes = snap.heap.freeBytes;
    sample.minFreeBytes = snap.heap.minFreeBytes;
    sample.largestFreeBlock = snap.heap.largestFreeBlock;

    heapWatchWriteIndex_ = (heapWatchWriteIndex_ + 1U) % kHeapWatchSampleCount;
    if (heapWatchCount_ < kHeapWatchSampleCount) {
        ++heapWatchCount_;
    }
}

void SystemMonitorModule::armHeapWatchDump_(const SystemStatsSnapshot& snap, const char* reason)
{
    heapWatchTripActive_ = true;
    heapWatchDumpPending_ = true;
    heapWatchTriggerMs_ = snap.uptimeMs;
    heapWatchTriggerFreeBytes_ = snap.heap.freeBytes;
    heapWatchTriggerMinFreeBytes_ = snap.heap.minFreeBytes;
    heapWatchTriggerLargestFreeBlock_ = snap.heap.largestFreeBlock;
    heapWatchFrozenWriteIndex_ = heapWatchWriteIndex_;
    heapWatchFrozenCount_ = heapWatchCount_;
    snprintf(heapWatchTriggerReason_, sizeof(heapWatchTriggerReason_), "%s", reason ? reason : "-");
#ifdef CONFIG_HEAP_TASK_TRACKING
    captureHeapWatchTaskTotals_();
#endif
    LOGW("HeapWatch trip captured reason=%s free=%lu min=%lu largest=%lu",
         heapWatchTriggerReason_,
         (unsigned long)heapWatchTriggerFreeBytes_,
         (unsigned long)heapWatchTriggerMinFreeBytes_,
         (unsigned long)heapWatchTriggerLargestFreeBlock_);
}

void SystemMonitorModule::dumpHeapWatchWindow_() const
{
    if (heapWatchFrozenCount_ == 0U) {
        LOGW("HeapWatch window unavailable");
        return;
    }

    const size_t sampleCount =
        (heapWatchFrozenCount_ < kHeapWatchDumpSampleCount) ? heapWatchFrozenCount_ : kHeapWatchDumpSampleCount;
    const size_t startIndex =
        (heapWatchFrozenWriteIndex_ + kHeapWatchSampleCount - sampleCount) % kHeapWatchSampleCount;

    for (size_t i = 0; i < sampleCount; ++i) {
        const size_t idx = (startIndex + i) % kHeapWatchSampleCount;
        const HeapWatchSample& sample = heapWatchSamples_[idx];
        const long dtMs = (long)sample.uptimeMs - (long)heapWatchTriggerMs_;
        LOGW("HeapWatch win dt=%ld free=%lu min=%lu largest=%lu",
             dtMs,
             (unsigned long)sample.freeBytes,
             (unsigned long)sample.minFreeBytes,
             (unsigned long)sample.largestFreeBlock);
    }
}

#ifdef CONFIG_HEAP_TASK_TRACKING
void SystemMonitorModule::captureHeapWatchTaskTotals_()
{
    heapWatchTaskTotalCount_ = 0U;

    heap_task_totals_t totals[kHeapWatchTaskTotalsMax]{};
    size_t numTotals = 0U;
    heap_task_info_params_t params{};
    params.caps[0] = 0;
    params.mask[0] = 0;
    params.totals = totals;
    params.num_totals = &numTotals;
    params.max_totals = kHeapWatchTaskTotalsMax;

    heap_caps_get_per_task_info(&params);

    const size_t totalCount = (numTotals < kHeapWatchTaskTotalsMax) ? numTotals : kHeapWatchTaskTotalsMax;
    for (size_t i = 0; i < totalCount; ++i) {
        heapWatchTaskTotals_[i].sizeBytes = (uint32_t)totals[i].size[0];
        heapWatchTaskTotals_[i].blockCount = (uint32_t)totals[i].count[0];
        copyTaskName_(heapWatchTaskTotals_[i].taskName, sizeof(heapWatchTaskTotals_[i].taskName), totals[i].task);
    }
    heapWatchTaskTotalCount_ = totalCount;
    for (size_t i = 0; i < heapWatchTaskTotalCount_; ++i) {
        size_t best = i;
        for (size_t j = i + 1; j < heapWatchTaskTotalCount_; ++j) {
            if (heapWatchTaskTotals_[j].sizeBytes > heapWatchTaskTotals_[best].sizeBytes) {
                best = j;
            }
        }
        if (best == i) continue;
        HeapWatchTaskTotal tmp = heapWatchTaskTotals_[i];
        heapWatchTaskTotals_[i] = heapWatchTaskTotals_[best];
        heapWatchTaskTotals_[best] = tmp;
    }
}

void SystemMonitorModule::dumpHeapWatchTaskTotals_() const
{
    if (heapWatchTaskTotalCount_ == 0U) {
        LOGW("HeapWatch tasks unavailable");
        return;
    }

    for (size_t i = 0; i < heapWatchTaskTotalCount_; ++i) {
        const HeapWatchTaskTotal& total = heapWatchTaskTotals_[i];
        if (total.sizeBytes == 0U && total.blockCount == 0U) continue;
        LOGW("HeapWatch task=%s alloc=%lu blocks=%lu",
             total.taskName[0] ? total.taskName : "-",
             (unsigned long)total.sizeBytes,
             (unsigned long)total.blockCount);
    }
}
#endif

void SystemMonitorModule::dumpHeapWatch_()
{
    LOGW("HeapWatch dump reason=%s trigger_free=%lu trigger_min=%lu trigger_largest=%lu captured=%u dump_last=%u sample_ms=%lu",
         heapWatchTriggerReason_[0] ? heapWatchTriggerReason_ : "-",
         (unsigned long)heapWatchTriggerFreeBytes_,
         (unsigned long)heapWatchTriggerMinFreeBytes_,
         (unsigned long)heapWatchTriggerLargestFreeBlock_,
         (unsigned)heapWatchFrozenCount_,
         (unsigned)kHeapWatchDumpSampleCount,
         (unsigned long)kHeapWatchSamplePeriodMs);
    dumpHeapWatchWindow_();
#ifdef CONFIG_HEAP_TASK_TRACKING
    dumpHeapWatchTaskTotals_();
#endif
    heapWatchDumpPending_ = false;
}

void SystemMonitorModule::pollHeapWatch_(uint32_t now)
{
    if (lastHeapWatchSampleMs_ != 0U && (uint32_t)(now - lastHeapWatchSampleMs_) < kHeapWatchSamplePeriodMs) {
        return;
    }
    lastHeapWatchSampleMs_ = now;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    appendHeapWatchSample_(snap);

    if (heapWatchLastSeenMinFree_ == UINT32_MAX) {
        heapWatchLastSeenMinFree_ = snap.heap.minFreeBytes;
    }

    const bool currentFreeTrip = snap.heap.freeBytes <= kHeapWatchTripFreeBytes;
    const bool minLowWaterTrip =
        snap.heap.minFreeBytes <= kHeapWatchTripFreeBytes && snap.heap.minFreeBytes < heapWatchLastSeenMinFree_;

    if (!heapWatchTripActive_ && !heapWatchDumpPending_) {
        if (currentFreeTrip) {
            armHeapWatchDump_(snap, "free");
        } else if (minLowWaterTrip) {
            armHeapWatchDump_(snap, "min_low");
        }
    }

    if (snap.heap.minFreeBytes < heapWatchLastSeenMinFree_) {
        heapWatchLastSeenMinFree_ = snap.heap.minFreeBytes;
    }

    if (heapWatchDumpPending_) {
        const bool recovered = snap.heap.freeBytes >= kHeapWatchRecoverFreeBytes;
        const bool timeout = (uint32_t)(snap.uptimeMs - heapWatchTriggerMs_) >= kHeapWatchDumpDelayMs;
        if (recovered || timeout) {
            dumpHeapWatch_();
        }
    }

    if (heapWatchTripActive_ && snap.heap.freeBytes >= kHeapWatchRecoverFreeBytes) {
        LOGI("HeapWatch recovered free=%lu min=%lu largest=%lu",
             (unsigned long)snap.heap.freeBytes,
             (unsigned long)snap.heap.minFreeBytes,
             (unsigned long)snap.heap.largestFreeBlock);
        heapWatchTripActive_ = false;
    }
}

void SystemMonitorModule::logPendingHeapAllocFailure_()
{
    if (!gHeapAllocFailedPending) return;

    const size_t size = gHeapAllocFailedSize;
    const uint32_t caps = gHeapAllocFailedCaps;
    const char* functionName = gHeapAllocFailedFunction;
    gHeapAllocFailedPending = false;

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);
    LOGW("Heap alloc failed size=%lu caps=0x%08lx func=%s free=%lu min=%lu largest=%lu",
         (unsigned long)size,
         (unsigned long)caps,
         functionName ? functionName : "-",
         (unsigned long)snap.heap.freeBytes,
         (unsigned long)snap.heap.minFreeBytes,
         (unsigned long)snap.heap.largestFreeBlock);
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
#if FLOW_WEB_HEAP_FORENSICS
    pollHeapWatch_(now);
    logPendingHeapAllocFailure_();
#endif
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

    vTaskDelay(pdMS_TO_TICKS(25));
}
