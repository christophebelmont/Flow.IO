#pragma once
/**
 * @file AlarmIds.h
 * @brief Central stable alarm identifier repository.
 */

#include <stdint.h>

/** Stable alarm identifiers exposed across modules, MQTT, and automations. */
enum class AlarmId : uint16_t {
    None = 0,

    // PoolLogic domain
    PoolPsiLow = 1000,
    PoolPsiHigh = 1001,
};

/** Alarm severity used for prioritization and summaries. */
enum class AlarmSeverity : uint8_t {
    Info = 0,
    Warning = 1,
    Alarm = 2,
    Critical = 3,
};
