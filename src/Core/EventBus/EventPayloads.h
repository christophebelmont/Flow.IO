#pragma once
/**
 * @file EventPayloads.h
 * @brief Payload types used by EventBus events.
 */
#include <stdint.h>
#include "Core/ConfigBranchIds.h"

// Keep payloads small and trivially copyable.
// EventBus will copy payload bytes into its queue buffer.

/** @brief Payload for ConfigChanged events. */
enum class ConfigModuleId : uint8_t {
    Unknown = 0,
    Wifi,
    Mqtt,
    Ha,
    Time,
    TimeScheduler,
    SystemMonitor,
    Io,
    I2cCfg,
    PoolLogic,
    PoolDevice,
    Alarms
};

/** @brief Payload for ConfigChanged events. */
struct ConfigChangedPayload {
    uint8_t moduleId = (uint8_t)ConfigModuleId::Unknown;
    uint16_t branchId = (uint16_t)ConfigBranchId::Unknown;
    char nvsKey[16];
    char module[24];
};
static_assert(sizeof(ConfigChangedPayload) <= 48, "ConfigChangedPayload too large for EventBus queue");

/** @brief Payload carrying network readiness information. */
struct WifiNetReadyPayload {
  uint8_t ip[4];
  uint8_t gw[4];
  uint8_t mask[4];
};

/** @brief Payload for sensor update notifications. */
struct SensorsUpdatedPayload {
    uint32_t tsMs;
};

/** @brief Payload for relay change events. */
struct RelayChangedPayload {
    uint8_t relayId;
    uint8_t state; // 0/1
};

/** @brief Payload for pool mode changes. */
struct PoolModeChangedPayload {
    uint8_t mode;
};

/** @brief Payload for alarm events. */
struct AlarmPayload {
    uint16_t alarmId;
};

/** @brief Payload for scheduler event trigger notifications. */
enum class SchedulerEdge : uint8_t {
    Trigger = 0,
    Start = 1,
    Stop = 2
};

/** @brief Payload for scheduler event trigger notifications. */
struct SchedulerEventTriggeredPayload {
    uint8_t slot;       // scheduler slot index [0..15]
    uint8_t edge;       // SchedulerEdge
    uint8_t replayed;   // 1 when replayed at boot/resync
    uint8_t reserved0 = 0;
    uint16_t eventId;   // consumer-level id
    uint16_t reserved1 = 0;
    uint64_t epochSec;
    uint16_t activeMask;
};

/** @brief Identifiers for DataStore values. */
using DataKey = uint16_t;

/** @brief Payload for data change events. */
struct DataChangedPayload {
    DataKey id;
};

/** @brief Dirty flags for snapshot payloads. */
enum DirtyFlags : uint32_t {
    DIRTY_NONE    = 0,
    DIRTY_NETWORK = 1 << 0,
    DIRTY_TIME    = 1 << 1,
    DIRTY_MQTT    = 1 << 2,
    DIRTY_SENSORS = 1 << 3,
    DIRTY_ACTUATORS = 1 << 4,
};

/** @brief Payload indicating a new data snapshot. */
struct DataSnapshotPayload {
    uint32_t dirtyFlags;
};
