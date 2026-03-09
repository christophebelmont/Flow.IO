#pragma once
/**
 * @file IMqtt.h
 * @brief MQTT publish service interface (job-based).
 */

#include <stddef.h>
#include <stdint.h>

enum class MqttPublishPriority : uint8_t {
    Low = 0,
    Normal = 1,
    High = 2,
};

enum class MqttBuildResult : uint8_t {
    Ready = 0,
    RetryLater = 1,
    NoLongerNeeded = 2,
    PermanentError = 3,
};

struct MqttBuildContext {
    char* topic = nullptr;
    uint16_t topicCapacity = 0;
    char* payload = nullptr;
    uint16_t payloadCapacity = 0;
    uint16_t topicLen = 0;
    uint16_t payloadLen = 0;
    uint8_t qos = 0;
    bool retain = false;
};

struct MqttPublishProducer {
    uint8_t producerId = 0;
    void* ctx = nullptr;
    MqttBuildResult (*buildMessage)(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx) = nullptr;
    void (*onMessagePublished)(void* ctx, uint16_t messageId) = nullptr;
    void (*onMessageDeferred)(void* ctx, uint16_t messageId) = nullptr;
    void (*onMessageDropped)(void* ctx, uint16_t messageId) = nullptr;
};

/** @brief Service wrapper exposed by MQTTModule. */
struct MqttService {
    bool (*enqueue)(void* ctx, uint8_t producerId, uint16_t messageId, uint8_t prio, uint8_t flags);
    bool (*registerProducer)(void* ctx, const MqttPublishProducer* producer);
    void (*formatTopic)(void* ctx, const char* suffix, char* out, size_t outLen);
    bool (*isConnected)(void* ctx);
    void* ctx;
};
