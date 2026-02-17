#pragma once
/**
 * @file SystemStats.h
 * @brief Lightweight system and heap stats helpers.
 */
#include <stdint.h>

/** @brief Heap snapshot (no dynamic allocation). */
struct HeapStats {
    uint32_t freeBytes;           // heap_caps_get_free_size(MALLOC_CAP_8BIT)
    uint32_t minFreeBytes;        // ESP.getMinFreeHeap()
    uint32_t largestFreeBlock;    // heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    uint8_t fragPercent;          // 0..100 estimated fragmentation
};

/** @brief Full system snapshot used by monitoring. */
struct SystemStatsSnapshot {
    uint64_t uptimeMs64;
    uint32_t uptimeMs;
    HeapStats heap;
};

/** @brief Lightweight helper (core, stateless). */
class SystemStats {
public:
    /** @brief Fill snapshot with current system metrics. */
    static void collect(SystemStatsSnapshot& out);

    /** @brief Reset reason as const string (ESP_RST_*). */
    static const char* resetReasonStr();
};
