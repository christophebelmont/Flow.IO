/**
 * @file SystemMonitorModule.cpp
 * @brief Implementation file.
 */
#include "SystemMonitorModule.h"
#include "Core/ModuleManager.h"   ///< required for iteration
#include <Arduino.h>
#include <WiFi.h>                ///< only for RSSI (optional)
#define LOG_TAG "SysMonit"
#include "Core/ModuleLog.h"


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
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::SystemMonitor;
    cfgStore_ = &cfg;
    cfg.registerVar(traceEnabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(tracePeriodVar_, kCfgModuleId, kCfgBranchId);

    wifiSvc = services.get<WifiService>("wifi");
    cfgSvc  = services.get<ConfigStoreService>("config");
    logHub  = services.get<LogHubService>("loghub");

    LOGI("Starting SystemMonitorModule");
    logBootInfo();
}

void SystemMonitorModule::logBootInfo() {
    LOGI("Reset reason=%s", SystemStats::resetReasonStr());
    LOGI("CPU=%luMHz", (unsigned long)ESP.getCpuFreqMHz());
}

void SystemMonitorModule::logHeapStats() {
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    LOGI("Heap free=%lu min=%lu largest=%lu frag=%u%%",
                (unsigned long)snap.heap.freeBytes,
                (unsigned long)snap.heap.minFreeBytes,
                (unsigned long)snap.heap.largestFreeBlock,
                (unsigned int)snap.heap.fragPercent);
}

void SystemMonitorModule::logTaskStacks() {
    if (!moduleManager) {
        LOGW("ModuleManager not set, task stats disabled");
        return;
    }

    char line[512];
    size_t off = 0;
    int w = snprintf(line, sizeof(line), "Stack");
    if (w < 0) return;
    off = (size_t)w;
    bool hasTask = false;
    bool hasLow = false;

    const uint8_t n = moduleManager->getCount();
    for (uint8_t i = 0; i < n; ++i) {
        Module* m = moduleManager->getModule(i);
        if (!m) continue;

        TaskHandle_t h = m->getTaskHandle();
        if (!h) continue;

        UBaseType_t hw = uxTaskGetStackHighWaterMark(h);
        hasTask = true;
        const bool isLow = (hw < 300);
        if (isLow) hasLow = true;

        if (off < sizeof(line)) {
            w = snprintf(line + off, sizeof(line) - off, " %s@c%ld=%u%s",
                         m->moduleId(),
                         (long)m->taskCore(),
                         (unsigned)hw,
                         isLow ? "!" : "");
            if (w < 0) break;
            if ((size_t)w >= (sizeof(line) - off)) {
                off = sizeof(line) - 1;
                line[off] = '\0';
                break;
            }
            off += (size_t)w;
        }
    }

    if (!hasTask) {
        LOGI("Stack none");
        return;
    }

    if (hasLow) {
        LOGW("%s", line);
    } else {
        LOGI("%s", line);
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
    const uint32_t now = millis();
    if (cfgStore_) {
        cfgStore_->logNvsWriteSummaryIfDue(now, 60000U);
    }

    uint32_t periodMs = (cfgData_.tracePeriodMs > 0) ? (uint32_t)cfgData_.tracePeriodMs : 5000U;
    uint32_t stackPeriodMs = periodMs * 6U;

    if (!cfgData_.traceEnabled) {
        vTaskDelay(pdMS_TO_TICKS(200));
        return;
    }

    if (lastTraceLogMs == 0U || (uint32_t)(now - lastTraceLogMs) >= periodMs) {
        lastTraceLogMs = now;
        logHeapStats();
    }

    if (lastStackDumpMs == 0U || (uint32_t)(now - lastStackDumpMs) >= stackPeriodMs) {
        lastStackDumpMs = now;
        logTaskStacks();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
}
