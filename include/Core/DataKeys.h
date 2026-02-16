#pragma once
/**
 * @file DataKeys.h
 * @brief Central registry and reserved ranges for DataStore keys.
 */

#include <stdint.h>

#include "Core/EventBus/EventPayloads.h"

namespace DataKeys {

/** @brief WiFi runtime key: connectivity ready state (`WifiRuntime`). */
constexpr DataKey WifiReady = 1;
/** @brief WiFi runtime key: IPv4 address (`WifiRuntime`). */
constexpr DataKey WifiIp = 2;
/** @brief Time runtime key: synchronized state (`TimeRuntime`). */
constexpr DataKey TimeReady = 3;
/** @brief MQTT runtime key: broker connected state (`MQTTRuntime`). */
constexpr DataKey MqttReady = 4;
/** @brief MQTT runtime key: dropped RX messages counter (`MQTTRuntime`). */
constexpr DataKey MqttRxDrop = 5;
/** @brief MQTT runtime key: RX JSON parse failures counter (`MQTTRuntime`). */
constexpr DataKey MqttParseFail = 6;
/** @brief MQTT runtime key: RX handler failures counter (`MQTTRuntime`). */
constexpr DataKey MqttHandlerFail = 7;
/** @brief MQTT runtime key: dropped RX messages due to oversize topic/payload (`MQTTRuntime`). */
constexpr DataKey MqttOversizeDrop = 8;

/** @brief Home Assistant runtime key: autoconfig publish state (`HARuntime`). */
constexpr DataKey HaPublished = 10;
/** @brief Home Assistant runtime key: configured vendor (`HARuntime`). */
constexpr DataKey HaVendor = 11;
/** @brief Home Assistant runtime key: configured device id (`HARuntime`). */
constexpr DataKey HaDeviceId = 12;

/** @brief Reserved base for IO endpoint runtime keys (`IORuntime`). */
constexpr DataKey IoBase = 40;
/** @brief Reserved IO runtime key count: supports endpoints `[0..23]`. */
constexpr uint8_t IoReservedCount = 24;
/** @brief End-exclusive bound for IO runtime key range. */
constexpr DataKey IoEndExclusive = IoBase + IoReservedCount;

/** @brief Reserved base for pool-device state runtime keys (`PoolDeviceRuntime`, state part). */
constexpr DataKey PoolDeviceStateBase = 80;
/** @brief Reserved pool-device state key count: supports slots `[0..7]`. */
constexpr uint8_t PoolDeviceStateReservedCount = 8;
/** @brief End-exclusive bound for pool-device state key range. */
constexpr DataKey PoolDeviceStateEndExclusive = PoolDeviceStateBase + PoolDeviceStateReservedCount;

/** @brief Reserved base for pool-device metrics runtime keys (`PoolDeviceRuntime`, metrics part). */
constexpr DataKey PoolDeviceMetricsBase = PoolDeviceStateEndExclusive;
/** @brief Reserved pool-device metrics key count: supports slots `[0..7]`. */
constexpr uint8_t PoolDeviceMetricsReservedCount = 8;
/** @brief End-exclusive bound for pool-device metrics key range. */
constexpr DataKey PoolDeviceMetricsEndExclusive = PoolDeviceMetricsBase + PoolDeviceMetricsReservedCount;

/** @brief Upper bound for currently reserved keys. */
constexpr DataKey ReservedMax = 127;

static_assert(WifiReady < TimeReady, "DataKey ordering invariant broken");
static_assert(TimeReady < MqttReady, "DataKey ordering invariant broken");
static_assert(MqttOversizeDrop < HaPublished, "DataKey ranges overlap");
static_assert(HaDeviceId < IoBase, "HA fixed keys overlap IO key range");
static_assert(IoEndExclusive <= PoolDeviceStateBase, "IO and pool-device key ranges overlap");
static_assert(PoolDeviceStateEndExclusive <= PoolDeviceMetricsBase, "Pool-device state and metrics ranges overlap");
static_assert(PoolDeviceMetricsEndExclusive <= ReservedMax, "Pool-device key range exceeds reserved max");

}  // namespace DataKeys
