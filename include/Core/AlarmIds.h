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
    PoolPhTankLow = 1002,
    PoolChlorineTankLow = 1003,

    // Log pipeline domain
    LogWarningSeen = 1100,
    LogErrorSeen = 1101,
};

/** Alarm severity used for prioritization and summaries. */
enum class AlarmSeverity : uint8_t {
    Info = 0,
    Warning = 1,
    Alarm = 2,
    Critical = 3,
};
