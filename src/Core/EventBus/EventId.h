#pragma once
/**
 * @file EventId.h
 * @brief Enumerates event identifiers used by EventBus.
 */
#include <stdint.h>

/** @brief Known event identifiers. */
enum class EventId : uint16_t {
    None = 0,

    // System lifecycle
    SystemStarted = 1,

    // ✅ DataStore (runtime model changes)
    DataChanged = 50,
    DataSnapshotAvailable = 51,

    // Configuration
    ConfigChanged = 100,

    // Sensors / runtime data
    SensorsUpdated = 200,

    // Actuators
    RelayChanged = 300,

    // Domain events
    PoolModeChanged = 400,
    AlarmRaised = 410,
    AlarmCleared = 411,
    AlarmAcked = 412,
    AlarmSilenceChanged = 413,
    SchedulerEventTriggered = 420,
};
