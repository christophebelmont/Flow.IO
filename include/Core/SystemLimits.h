#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file SystemLimits.h
 * @brief Shared compile-time limits used across Core and modules.
 */

namespace Limits {

/** @brief MQTT full topic buffer length used by runtime snapshot publications in `main.cpp`. */
constexpr size_t TopicBuf = 70;
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
/** @brief Maximum number of registered config variables in `ConfigStore` metadata table.
 *  Sized for the current FlowIO profile with modest headroom while staying within DRAM budget. */
constexpr size_t MaxConfigVars = 368;
/** @brief Maximum NVS key length (without null terminator) enforced by `ConfigTypes::NVS_KEY`. */
constexpr size_t MaxNvsKeyLen = 15;
/** @brief FreeRTOS log queue length used by `LogHub` (`LogHubModule::init`).
 *  Increased moderately to absorb boot-time log bursts before log dispatcher task starts. */
constexpr uint8_t LogQueueLen = 64;
/** @brief FreeRTOS event queue length used by `EventBus` (`EventBus::QUEUE_LENGTH`).
 *  Sized to absorb startup bursts while limiting DRAM usage. */
constexpr uint8_t EventQueueLen = 32;
/** @brief Maximum number of EventBus subscribers (`EventBus::MAX_SUBSCRIBERS`). */
constexpr uint8_t EventSubscribersMax = 50;

/** @brief Core runtime limits shared by the module framework. */
namespace Core {
namespace Capacity {
/** @brief Maximum number of modules registered in `ModuleManager`. */
constexpr size_t MaxModules = 25;
/** @brief Maximum number of declared tasks tracked by `ModuleManager`. */
constexpr size_t MaxModuleTasks = 32;
}  // namespace Capacity

namespace Task {
/** @brief Default FreeRTOS task stack size used by `Module::taskStackSize`. */
constexpr uint16_t DefaultStackSize = 3072;
}  // namespace Task

namespace Timing {
/** @brief Small cooperative delay inserted after each `Module::loop` execution. */
constexpr uint32_t LoopDelayMs = 10;
}  // namespace Timing
}  // namespace Core

/** @brief Configuration tree limits shared by local and remote explorers. */
namespace Config {
namespace Capacity {
/** @brief Maximum number of unique config branches returned by `ConfigStore::listModules`. */
constexpr uint8_t ModuleListMax = 96;
}  // namespace Capacity
}  // namespace Config

/** @brief MQTT-specific limits grouped by concern to keep `SystemLimits` readable. */
namespace Mqtt {

/** @brief MQTT module task stack size returned by `MQTTModule::taskStackSize`. */
constexpr uint16_t TaskStackSize = 5712;

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
constexpr size_t BaseTopic = 15;
/** @brief MQTT device identifier buffer length used by `MQTTModule::deviceId` (e.g. `ESP32-XXXXXX`). */
constexpr size_t DeviceId = 15;
/** @brief MQTT full topic buffer length used by `MQTTModule` fixed topics (`cmd`, `ack`, `status`, `cfg/*`). */
constexpr size_t Topic = 70;
/** @brief MQTT temporary topic buffer length for dynamic subtopics in `MQTTModule` (`cfg/<module>`, scheduler slots). */
constexpr size_t DynamicTopic = 160;
/** @brief RX command topic buffer length inside `MQTTModule::RxMsg`. */
constexpr size_t RxTopic = 128;
/** @brief RX command payload buffer length inside `MQTTModule::RxMsg`. */
constexpr size_t RxPayload = 384;
/** @brief ACK JSON buffer length used by command/config acknowledge payloads. */
constexpr size_t Ack = 1536;
/** @brief Command handler reply buffer length used by `MQTTModule` (`replyBuf`).
 *  Must accommodate larger structured replies (e.g. `alarms.list` snapshots). */
constexpr size_t Reply = 1024;
/** @brief Config JSON serialization buffer length used by module/state exports. */
constexpr size_t StateCfg = 1536;
/** @brief Runtime publish payload buffer length used by `MQTTModule` shared scratch buffer. */
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
/** @brief Delay in ms between each retained `cfg/<module>` publish during startup ramp in `MQTTModule`. */
constexpr uint32_t CfgRampStepMs = 300;
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
constexpr uint8_t MaxRuntimeRoutes = 36;
/** @brief Default momentary digital output pulse duration in ms (`IOModule`). */
constexpr uint16_t MomentaryPulseMs = 500;
/** @brief Default periodic trace interval for ORP/pH/PSI calc logs (`IOModule`, `trace_period_ms`). */
constexpr uint32_t IoTracePeriodMs = 10000;
/** @brief HA command payload buffer length for IO output switches (`IOModule::haSwitchPayloadOn_/Off_`). */
constexpr size_t IoHaSwitchPayloadBuf = 128;

/** @brief Alarm engine compile-time capacities and defaults. */
namespace Alarm {
/** @brief Maximum number of alarm slots managed by `AlarmModule`. */
constexpr uint16_t MaxAlarms = 16;
/** @brief Maximum JSON buffer used for alarm snapshot serialization. */
constexpr size_t SnapshotJsonBuf = 1536;
/** @brief Default alarm evaluation period in ms (`AlarmModule::loop`). */
constexpr uint32_t DefaultEvalPeriodMs = 250;
/** @brief JSON capacity for alarm command args parsing. */
constexpr size_t JsonCmdBuf = 256;
}  // namespace Alarm

/** @brief Shared WiFi runtime timings and buffers. */
namespace Wifi {
namespace Buffers {
/** @brief JSON document/output buffer used for WiFi scan status snapshots. */
constexpr size_t ScanStatusJson = 3072;
}  // namespace Buffers

namespace Timing {
/** @brief Minimum spacing between scan attempts when throttling WiFi scans. */
constexpr uint32_t ScanThrottleMs = 8000U;
/** @brief Initial delay before the first STA connect attempt after boot/config load. */
constexpr uint32_t InitialConnectDelayMs = 1200U;
/** @brief Startup window during which disconnect logs stay at debug level. */
constexpr uint32_t StartupTransientLogWindowMs = 10000U;
/** @brief Minimum spacing between repeated empty-SSID warnings. */
constexpr uint32_t EmptySsidLogIntervalMs = 10000U;
/** @brief Loop delay while WiFi runtime is disabled. */
constexpr uint32_t DisabledLoopDelayMs = 2000U;
/** @brief Loop delay while retries are disabled and WiFi is idle. */
constexpr uint32_t IdleRetryDisabledLoopDelayMs = 500U;
/** @brief Poll delay while waiting for the initial connect deadline. */
constexpr uint32_t IdleConnectPollDelayMs = 200U;
/** @brief Delay after issuing a STA connect attempt. */
constexpr uint32_t IdlePostConnectDelayMs = 1000U;
/** @brief Log cadence while the STA connect attempt is in progress. */
constexpr uint32_t ConnectingLogIntervalMs = 3000U;
/** @brief Delay before forcing a reconnect kick during connection attempts. */
constexpr uint32_t ReconnectKickDelayMs = 4000U;
/** @brief Timeout for a WiFi STA connect attempt before entering error wait. */
constexpr uint32_t ConnectTimeoutMs = 15000U;
/** @brief Loop delay while waiting for a WiFi STA connection. */
constexpr uint32_t ConnectingLoopDelayMs = 200U;
/** @brief Loop delay while WiFi remains connected. */
constexpr uint32_t ConnectedLoopDelayMs = 1000U;
/** @brief Error wait duration before retrying a failed WiFi connection. */
constexpr uint32_t ErrorWaitDurationMs = 5000U;
/** @brief Loop delay while staying in WiFi error-wait state. */
constexpr uint32_t ErrorWaitLoopDelayMs = 500U;
}  // namespace Timing
}  // namespace Wifi

/** @brief Home Assistant auto-discovery publication pacing limits. */
namespace Ha {
namespace Timing {
/** @brief Delay in ms between each HA discovery entity publish in `HAModule`. */
constexpr uint32_t DiscoveryStepMs = 200;
}  // namespace Timing
}  // namespace Ha

/** @brief Shared heap guards before network publishes (MQTT and HA discovery). */
namespace NetworkPublish {
/** @brief Minimum free 8-bit heap (bytes) required before attempting publish. */
constexpr uint32_t MinFreeHeapBytes = 4000U;
/** @brief Minimum largest 8-bit free block (bytes) required before attempting publish. */
constexpr uint32_t MinLargestBlockBytes = 4096U;
}  // namespace NetworkPublish

/** @brief Boot orchestration timings used in `main.cpp` staged startup. */
namespace Boot {
/** @brief Delay in ms before allowing MQTT connection attempts (`MQTTModule::setStartupReady`). */
constexpr uint32_t MqttStartDelayMs = 1500;
/** @brief Delay in ms before enabling HA auto-discovery publishing (`HAModule::setStartupReady`). */
constexpr uint32_t HaStartDelayMs = 15000;
/** @brief Delay in ms before enabling PoolLogic control loop (`PoolLogicModule::setStartupReady`). */
constexpr uint32_t PoolLogicStartDelayMs = 10000;
/** @brief Delay in ms before releasing the Supervisor web task (`WebInterfaceModule`). */
constexpr uint32_t WebInterfaceStartDelayMs = 10000;
}  // namespace Boot

/** @brief Firmware update networking limits shared by the update module. */
namespace FirmwareUpdate {
namespace Http {
/** @brief HTTP connect timeout used for firmware and cfgdocs downloads. */
constexpr uint16_t ConnectTimeoutMs = 15000U;
/** @brief HTTP request timeout used once the download has started. */
constexpr uint32_t RequestTimeoutMs = 60000U;
/** @brief Timeout while waiting for the next chunk from a download stream. */
constexpr uint32_t StreamReadTimeoutMs = 15000U;
/** @brief Scratch buffer size used to copy download streams chunk by chunk. */
constexpr size_t StreamChunkBytes = 1024U;
}  // namespace Http
}  // namespace FirmwareUpdate

}  // namespace Limits
