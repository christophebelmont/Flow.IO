#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file SystemLimits.h
 * @brief Shared compile-time limits used across Core and modules.
 */

namespace Limits {

/** @brief MQTT topic buffer length used by runtime snapshot routing in `main.cpp`. */
constexpr size_t TopicBuf = 128;
/** @brief JSON capacity for MQTT cfg patch parsing in `MQTTModule::publishConfigBlocksFromPatch`. */
constexpr size_t JsonPatchBuf = 1024;
/** @brief JSON capacity for MQTT `cmd` payload parsing in `MQTTModule::processRxCmd_`. */
constexpr size_t JsonCmdBuf = 1024;
/** @brief JSON capacity for MQTT `cfg/set` payload parsing in `MQTTModule::processRxCfgSet_`. */
constexpr size_t JsonCfgBuf = 1024;
/** @brief JSON capacity for command args parsing in `TimeModule::parseCmdArgsObject_`. */
constexpr size_t JsonCmdTimeBuf = 768;
/** @brief JSON capacity for command args parsing in `PoolDeviceModule::parseCmdArgsObject_`. */
constexpr size_t JsonCmdPoolDeviceBuf = 256;
/** @brief JSON capacity for `ConfigStore::applyJson` root document (covers full multi-module patch). */
constexpr size_t JsonConfigApplyBuf = JsonCfgBuf * 4;
/** @brief Maximum number of registered config variables in `ConfigStore` metadata table. */
constexpr size_t MaxConfigVars = 500;
/** @brief Maximum NVS key length (without null terminator) enforced by `ConfigTypes::NVS_KEY`. */
constexpr size_t MaxNvsKeyLen = 15;
/** @brief FreeRTOS log queue length used by `LogHub` (`LogHubModule::init`). */
constexpr uint8_t LogQueueLen = 32;
/** @brief FreeRTOS event queue length used by `EventBus` (`EventBus::QUEUE_LENGTH`). */
constexpr uint8_t EventQueueLen = 16;
/** @brief FreeRTOS RX queue length for inbound MQTT messages in `MQTTModule`. */
constexpr uint8_t MqttRxQueueLen = 8;
/** @brief Minimum MQTT reconnect backoff in ms (`MQTTModule` error-wait state). */
constexpr uint32_t MqttBackoffMinMs = 2000;
/** @brief MQTT reconnect backoff step #1 threshold in ms. */
constexpr uint32_t MqttBackoffStep1Ms = 5000;
/** @brief MQTT reconnect backoff step #2 threshold in ms. */
constexpr uint32_t MqttBackoffStep2Ms = 10000;
/** @brief MQTT reconnect backoff step #3 threshold in ms. */
constexpr uint32_t MqttBackoffStep3Ms = 30000;
/** @brief MQTT reconnect backoff step #4 threshold in ms. */
constexpr uint32_t MqttBackoffStep4Ms = 60000;
/** @brief Maximum MQTT reconnect backoff in ms. */
constexpr uint32_t MqttBackoffMaxMs = 300000;
/** @brief Random jitter percentage applied to MQTT reconnect backoff delay. */
constexpr uint8_t MqttBackoffJitterPct = 15;
/** @brief Main MQTT task loop delay in ms (`MQTTModule::loop`). */
constexpr uint32_t MqttLoopDelayMs = 50;
/** @brief Maximum number of runtime MQTT routes stored in the runtime mux (`main.cpp`). */
constexpr uint8_t MaxRuntimeRoutes = 32;
/** @brief Default momentary digital output pulse duration in ms (`IOModule`). */
constexpr uint16_t MomentaryPulseMs = 500;

}  // namespace Limits
