/**
 * @file SystemStats.cpp
 * @brief Implementation file.
 */
#include "SystemStats.h"

#include <Arduino.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#define LOG_TAG_CORE "SysStats"

void SystemStats::collect(SystemStatsSnapshot& out) {
    const uint64_t uptimeMs64 = (uint64_t)(esp_timer_get_time() / 1000ULL);
    out.uptimeMs64 = uptimeMs64;
    // Keep the legacy 32-bit view for code paths that still expect uint32_t.
    out.uptimeMs = (uint32_t)uptimeMs64;

    const uint32_t free8 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    out.heap.freeBytes = free8;
    out.heap.minFreeBytes = ESP.getMinFreeHeap();
    out.heap.largestFreeBlock = largest;

    /// Fragmentation estimation:
    /// - if largest is close to free => low fragmentation
    /// - if largest is much smaller => high fragmentation
    if (free8 == 0) {
        out.heap.fragPercent = 100;
    } else {
        float ratio = (float)largest / (float)free8;   ///< 0..1
        float frag = 1.0f - ratio;                     ///< 0..1
        if (frag < 0.0f) frag = 0.0f;
        if (frag > 1.0f) frag = 1.0f;
        out.heap.fragPercent = (uint8_t)(frag * 100.0f);
    }
}

const char* SystemStats::resetReasonStr() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
    }
}
