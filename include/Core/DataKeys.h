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

/** @brief Reserved base for mirrored Flow runtime keys on Supervisor. */
constexpr DataKey FlowRemoteBase = 128;
constexpr DataKey FlowRemoteReady = FlowRemoteBase + 0;
constexpr DataKey FlowRemoteLinkOk = FlowRemoteBase + 1;
constexpr DataKey FlowRemoteFirmware = FlowRemoteBase + 2;
constexpr DataKey FlowRemoteHasRssi = FlowRemoteBase + 3;
constexpr DataKey FlowRemoteRssiDbm = FlowRemoteBase + 4;
constexpr DataKey FlowRemoteHasHeapFrag = FlowRemoteBase + 5;
constexpr DataKey FlowRemoteHeapFragPct = FlowRemoteBase + 6;
constexpr DataKey FlowRemoteMqttReady = FlowRemoteBase + 7;
constexpr DataKey FlowRemoteMqttRxDrop = FlowRemoteBase + 8;
constexpr DataKey FlowRemoteMqttParseFail = FlowRemoteBase + 9;
constexpr DataKey FlowRemoteI2cReqCount = FlowRemoteBase + 10;
constexpr DataKey FlowRemoteI2cBadReqCount = FlowRemoteBase + 11;
constexpr DataKey FlowRemoteI2cLastReqAgoMs = FlowRemoteBase + 12;
constexpr DataKey FlowRemoteHasPoolModes = FlowRemoteBase + 13;
constexpr DataKey FlowRemoteFiltrationAuto = FlowRemoteBase + 14;
constexpr DataKey FlowRemoteWinterMode = FlowRemoteBase + 15;
constexpr DataKey FlowRemotePhAutoMode = FlowRemoteBase + 16;
constexpr DataKey FlowRemoteOrpAutoMode = FlowRemoteBase + 17;
constexpr DataKey FlowRemoteFiltrationOn = FlowRemoteBase + 18;
constexpr DataKey FlowRemotePhPumpOn = FlowRemoteBase + 19;
constexpr DataKey FlowRemoteChlorinePumpOn = FlowRemoteBase + 20;
constexpr DataKey FlowRemoteHasPh = FlowRemoteBase + 21;
constexpr DataKey FlowRemotePhValue = FlowRemoteBase + 22;
constexpr DataKey FlowRemoteHasOrp = FlowRemoteBase + 23;
constexpr DataKey FlowRemoteOrpValue = FlowRemoteBase + 24;
constexpr DataKey FlowRemoteHasWaterTemp = FlowRemoteBase + 25;
constexpr DataKey FlowRemoteWaterTemp = FlowRemoteBase + 26;
constexpr DataKey FlowRemoteHasAirTemp = FlowRemoteBase + 27;
constexpr DataKey FlowRemoteAirTemp = FlowRemoteBase + 28;
constexpr uint8_t FlowRemoteReservedCount = 32;
constexpr DataKey FlowRemoteEndExclusive = FlowRemoteBase + FlowRemoteReservedCount;

/** @brief Upper bound for currently reserved keys. */
constexpr DataKey ReservedMax = FlowRemoteEndExclusive - 1;

static_assert(WifiReady < TimeReady, "DataKey ordering invariant broken");
static_assert(TimeReady < MqttReady, "DataKey ordering invariant broken");
static_assert(MqttOversizeDrop < HaPublished, "DataKey ranges overlap");
static_assert(HaDeviceId < IoBase, "HA fixed keys overlap IO key range");
static_assert(IoEndExclusive <= PoolDeviceStateBase, "IO and pool-device key ranges overlap");
static_assert(PoolDeviceStateEndExclusive <= PoolDeviceMetricsBase, "Pool-device state and metrics ranges overlap");
static_assert(PoolDeviceMetricsEndExclusive <= FlowRemoteBase, "Pool-device and flow-remote key ranges overlap");
static_assert((FlowRemoteBase + FlowRemoteReservedCount) == FlowRemoteEndExclusive, "Flow remote key bounds inconsistent");
static_assert(FlowRemoteEndExclusive <= (ReservedMax + 1), "Flow remote key range exceeds reserved max");

}  // namespace DataKeys
