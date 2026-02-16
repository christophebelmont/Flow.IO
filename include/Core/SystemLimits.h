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
/** @brief MQTT-specific limits grouped by concern to keep `SystemLimits` readable. */
namespace Mqtt {

/** @brief MQTT module task stack size returned by `MQTTModule::taskStackSize`. */
constexpr uint16_t TaskStackSize = 6144;

/** @brief MQTT static capacities (queues, tables). */
namespace Capacity {
/** @brief FreeRTOS RX queue length for inbound MQTT messages in `MQTTModule`. */
constexpr uint8_t RxQueueLen = 8;
/** @brief Maximum number of runtime publishers stored in `MQTTModule::publishers`. */
constexpr uint8_t MaxPublishers = 8;
/** @brief Maximum number of `cfg/<module>` blocks tracked by `MQTTModule::cfgModules/topicCfgBlocks`. */
constexpr uint8_t CfgTopicMax = 48;
}  // namespace Capacity

/** @brief MQTT default configuration values. */
namespace Defaults {
/** @brief Default MQTT broker port used by `MQTTConfig::port` in `MQTTModule`. */
constexpr int32_t Port = 1883;
/** @brief Default minimum runtime publish period in ms for `mqtt.sensor_min_publish_ms`. */
constexpr uint32_t SensorMinPublishMs = 20000;
}  // namespace Defaults

/** @brief MQTT string/payload buffer sizes. */
namespace Buffers {
/** @brief MQTT config buffer length for `MQTTConfig::host` in `MQTTModule`. */
constexpr size_t Host = 64;
/** @brief MQTT config buffer length for `MQTTConfig::user` in `MQTTModule`. */
constexpr size_t User = 32;
/** @brief MQTT config buffer length for `MQTTConfig::pass` in `MQTTModule`. */
constexpr size_t Pass = 32;
/** @brief MQTT config buffer length for `MQTTConfig::baseTopic` in `MQTTModule`. */
constexpr size_t BaseTopic = 64;
/** @brief MQTT device identifier buffer length used by `MQTTModule::deviceId` (e.g. `ESP32-XXXXXX`). */
constexpr size_t DeviceId = 24;
/** @brief MQTT topic buffer length used by `MQTTModule` fixed topics (`cmd`, `ack`, `status`, `cfg/*`). */
constexpr size_t Topic = 128;
/** @brief MQTT temporary topic buffer length for dynamic subtopics in `MQTTModule` (`cfg/<module>`, scheduler slots). */
constexpr size_t DynamicTopic = 160;
/** @brief RX command topic buffer length inside `MQTTModule::RxMsg`. */
constexpr size_t RxTopic = 128;
/** @brief RX command payload buffer length inside `MQTTModule::RxMsg`. */
constexpr size_t RxPayload = 384;
/** @brief ACK JSON buffer length used by `MQTTModule` (`ackBuf`). */
constexpr size_t Ack = 512;
/** @brief Command handler reply buffer length used by `MQTTModule` (`replyBuf`). */
constexpr size_t Reply = 256;
/** @brief Config JSON serialization buffer length used by `MQTTModule` (`stateCfgBuf`). */
constexpr size_t StateCfg = 768;
/** @brief Runtime publish payload buffer length used by `MQTTModule` (`publishBuf`). */
constexpr size_t Publish = 1536;
/** @brief Parsed command name buffer length in `MQTTModule::processRxCmd_`. */
constexpr size_t CmdName = 64;
/** @brief Serialized command args JSON buffer length in `MQTTModule::processRxCmd_`. */
constexpr size_t CmdArgs = 320;
/** @brief Command module token buffer length in `MQTTModule::processRxCmd_`. */
constexpr size_t CmdModule = 32;
}  // namespace Buffers

/** @brief MQTT timing constants (runtime behavior). */
namespace Timing {
/** @brief Suppression window in ms for duplicate config echo events in `MQTTModule`. */
constexpr uint32_t CfgEchoSuppressMs = 1500;
/** @brief Startup retry window in ms for forced actuator runtime publishes in `MQTTModule`. */
constexpr uint32_t StartupActuatorRetryMs = 3000;
/** @brief Delay in ms while MQTT is disabled in `MQTTModule::loop`. */
constexpr uint32_t DisabledDelayMs = 2000;
/** @brief Network warmup delay in ms before first MQTT connect attempt in `MQTTModule::loop`. */
constexpr uint32_t NetWarmupMs = 2000;
/** @brief MQTT connection timeout in ms before forcing reconnect in `MQTTModule::loop`. */
constexpr uint32_t ConnectTimeoutMs = 10000;
/** @brief Main MQTT task loop delay in ms (`MQTTModule::loop`). */
constexpr uint32_t LoopDelayMs = 50;
}  // namespace Timing

/** @brief MQTT reconnect backoff profile. */
namespace Backoff {
/** @brief Minimum MQTT reconnect backoff in ms (`MQTTModule` error-wait state). */
constexpr uint32_t MinMs = 2000;
/** @brief MQTT reconnect backoff step #1 threshold in ms. */
constexpr uint32_t Step1Ms = 5000;
/** @brief MQTT reconnect backoff step #2 threshold in ms. */
constexpr uint32_t Step2Ms = 10000;
/** @brief MQTT reconnect backoff step #3 threshold in ms. */
constexpr uint32_t Step3Ms = 30000;
/** @brief MQTT reconnect backoff step #4 threshold in ms. */
constexpr uint32_t Step4Ms = 60000;
/** @brief Maximum MQTT reconnect backoff in ms. */
constexpr uint32_t MaxMs = 300000;
/** @brief Random jitter percentage applied to MQTT reconnect backoff delay. */
constexpr uint8_t JitterPct = 15;
}  // namespace Backoff

}  // namespace Mqtt
/** @brief Maximum number of runtime MQTT routes stored in the runtime mux (`main.cpp`). */
constexpr uint8_t MaxRuntimeRoutes = 32;
/** @brief Default momentary digital output pulse duration in ms (`IOModule`). */
constexpr uint16_t MomentaryPulseMs = 500;
/** @brief Default periodic trace interval for ORP/pH/PSI calc logs (`IOModule`, `trace_period_ms`). */
constexpr uint32_t IoTracePeriodMs = 10000;
/** @brief HA command payload buffer length for IO output switches (`IOModule::haSwitchPayloadOn_/Off_`). */
constexpr size_t IoHaSwitchPayloadBuf = 128;

}  // namespace Limits
