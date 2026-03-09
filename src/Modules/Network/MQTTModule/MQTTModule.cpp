/**
 * @file MQTTModule.cpp
 * @brief Unified MQTT TX implementation.
 */

#include "MQTTModule.h"

#include "Core/Runtime.h"
#include "Core/MqttTopics.h"
#include "Core/SystemLimits.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/DataKeys.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <initializer_list>
#include <new>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::MQTTModule)
#include "Core/ModuleLog.h"

static uint32_t clampU32(uint32_t v, uint32_t minV, uint32_t maxV)
{
    if (v < minV) return minV;
    if (v > maxV) return maxV;
    return v;
}

static uint32_t jitterMs(uint32_t baseMs, uint8_t pct)
{
    if (baseMs == 0U || pct == 0U) return baseMs;
    const uint32_t span = (baseMs * pct) / 100U;
    const uint32_t r = esp_random();
    const uint32_t delta = r % (2U * span + 1U);
    const int32_t signedDelta = (int32_t)delta - (int32_t)span;
    int32_t out = (int32_t)baseMs + signedDelta;
    if (out < 0) out = 0;
    return (uint32_t)out;
}

static bool isAnyOf(const char* key, std::initializer_list<const char*> keys)
{
    if (!key || key[0] == '\0') return false;
    for (const char* candidate : keys) {
        if (candidate && strcmp(key, candidate) == 0) return true;
    }
    return false;
}

static bool isMqttConnKey(const char* key)
{
    return isAnyOf(key, {
        NvsKeys::Mqtt::BaseTopic,
        NvsKeys::Mqtt::Host,
        NvsKeys::Mqtt::Port,
        NvsKeys::Mqtt::User,
        NvsKeys::Mqtt::Pass
    });
}

static void makeDeviceId(char* out, size_t len)
{
    if (!out || len == 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static constexpr uint8_t kMqttCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kMqttCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Mqtt, kMqttCfgBranch}, "mqtt", "mqtt", (uint8_t)MqttPublishPriority::Normal, nullptr},
};

bool MQTTModule::svcEnqueue_(void* ctx, uint8_t producerId, uint16_t messageId, uint8_t prio, uint8_t flags)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    if (!self) return false;

    MqttPublishPriority p = MqttPublishPriority::Normal;
    if (prio == (uint8_t)MqttPublishPriority::Low) p = MqttPublishPriority::Low;
    else if (prio == (uint8_t)MqttPublishPriority::High) p = MqttPublishPriority::High;

    return self->enqueue(producerId, messageId, p, flags);
}

bool MQTTModule::svcRegisterProducer_(void* ctx, const MqttPublishProducer* producer)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->registerProducer(producer) : false;
}

void MQTTModule::svcFormatTopic_(void* ctx, const char* suffix, char* out, size_t outLen)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    if (!self) return;
    self->formatTopic(out, outLen, suffix);
}

bool MQTTModule::svcIsConnected_(void* ctx)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->isConnected() : false;
}

void MQTTModule::setState_(MQTTState s)
{
    state_ = s;
    stateTs_ = millis();
    if (dataStore_) {
        setMqttReady(*dataStore_, s == MQTTState::Connected);
    }
}

void MQTTModule::buildTopics_()
{
    snprintf(topicCmd_, sizeof(topicCmd_), "%s/%s/%s", cfgData_.baseTopic, deviceId_, MqttTopics::SuffixCmd);
    snprintf(topicStatus_, sizeof(topicStatus_), "%s/%s/%s", cfgData_.baseTopic, deviceId_, MqttTopics::SuffixStatus);
    snprintf(topicCfgSet_, sizeof(topicCfgSet_), "%s/%s/%s", cfgData_.baseTopic, deviceId_, MqttTopics::SuffixCfgSet);
}

bool MQTTModule::registerRuntimeProvider(const IRuntimeSnapshotProvider* provider)
{
    return runtimeProducerCore_.registerProvider(provider);
}

bool MQTTModule::addRuntimePublisher(const char* topic,
                                     uint32_t periodMs,
                                     int qos,
                                     bool retain,
                                     bool (*build)(MQTTModule* self, char* out, size_t outLen),
                                     bool allowNoPayload)
{
    if (!topic || !build) return false;
    if (runtimePublisherCount_ >= Limits::Mqtt::Capacity::MaxPublishers) return false;

    RuntimePublisher& p = runtimePublishers_[runtimePublisherCount_];
    p.used = true;
    p.messageId = (uint16_t)(StatusMsgRuntimeBase + runtimePublisherCount_);
    p.topic = topic;
    p.periodMs = periodMs;
    p.nextDueMs = 0;
    p.qos = (qos < 0) ? 0U : (uint8_t)qos;
    p.retain = retain;
    p.build = build;
    p.allowNoPayload = allowNoPayload;
    ++runtimePublisherCount_;
    return true;
}

bool MQTTModule::registerProducer(const MqttPublishProducer* producer)
{
    if (!producer) return false;
    if (producer->producerId == 0U) return false;
    if (!producer->buildMessage) return false;

    bool ok = false;
    portENTER_CRITICAL(&producerMux_);
    for (uint8_t i = 0; i < producerCount_; ++i) {
        if (producers_[i] && producers_[i]->producerId == producer->producerId) {
            producers_[i] = producer;
            ok = true;
            break;
        }
    }

    if (!ok) {
        if (producerCount_ < MaxProducers) {
            producers_[producerCount_++] = producer;
            ok = true;
        }
    }
    portEXIT_CRITICAL(&producerMux_);
    return ok;
}

const MqttPublishProducer* MQTTModule::findProducer_(uint8_t producerId) const
{
    const MqttPublishProducer* out = nullptr;
    portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&producerMux_));
    for (uint8_t i = 0; i < producerCount_; ++i) {
        const MqttPublishProducer* p = producers_[i];
        if (p && p->producerId == producerId) {
            out = p;
            break;
        }
    }
    portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&producerMux_));
    return out;
}

int16_t MQTTModule::findJobSlot_(uint8_t producerId, uint16_t messageId) const
{
    for (uint8_t i = 0; i < MaxJobs; ++i) {
        const Job& job = jobs_[i];
        if (!job.used) continue;
        if (job.producerId == producerId && job.messageId == messageId) return (int16_t)i;
    }
    return -1;
}

int16_t MQTTModule::allocJobSlot_()
{
    for (uint8_t i = 0; i < MaxJobs; ++i) {
        if (!jobs_[i].used) return (int16_t)i;
    }
    return -1;
}

bool MQTTModule::queuePush_(uint8_t prio, const JobQueueItem& item)
{
    if (prio == (uint8_t)MqttPublishPriority::High) {
        if (highQ_.count >= HighQueueCap) return false;
        highQ_.items[highQ_.tail] = item;
        highQ_.tail = (uint16_t)((highQ_.tail + 1U) % HighQueueCap);
        ++highQ_.count;
        return true;
    }
    if (prio == (uint8_t)MqttPublishPriority::Normal) {
        if (normalQ_.count >= NormalQueueCap) return false;
        normalQ_.items[normalQ_.tail] = item;
        normalQ_.tail = (uint16_t)((normalQ_.tail + 1U) % NormalQueueCap);
        ++normalQ_.count;
        return true;
    }
    if (lowQ_.count >= LowQueueCap) return false;
    lowQ_.items[lowQ_.tail] = item;
    lowQ_.tail = (uint16_t)((lowQ_.tail + 1U) % LowQueueCap);
    ++lowQ_.count;
    return true;
}

bool MQTTModule::queuePop_(uint8_t prio, JobQueueItem& out)
{
    if (prio == (uint8_t)MqttPublishPriority::High) {
        if (highQ_.count == 0U) return false;
        out = highQ_.items[highQ_.head];
        highQ_.head = (uint16_t)((highQ_.head + 1U) % HighQueueCap);
        --highQ_.count;
        return true;
    }
    if (prio == (uint8_t)MqttPublishPriority::Normal) {
        if (normalQ_.count == 0U) return false;
        out = normalQ_.items[normalQ_.head];
        normalQ_.head = (uint16_t)((normalQ_.head + 1U) % NormalQueueCap);
        --normalQ_.count;
        return true;
    }
    if (lowQ_.count == 0U) return false;
    out = lowQ_.items[lowQ_.head];
    lowQ_.head = (uint16_t)((lowQ_.head + 1U) % LowQueueCap);
    --lowQ_.count;
    return true;
}

bool MQTTModule::queueSlot_(uint8_t slotIdx, uint8_t prio, bool invalidateOld)
{
    if (slotIdx >= MaxJobs) return false;
    Job& job = jobs_[slotIdx];
    if (!job.used) return false;

    if (invalidateOld && job.queued) {
        ++job.queueToken;
        job.queued = false;
    }

    if (job.queued) return true;

    job.queuedPrio = prio;
    job.queued = true;
    ++job.queueToken;

    JobQueueItem item{};
    item.slot = slotIdx;
    item.token = job.queueToken;

    if (!queuePush_(prio, item)) {
        job.queued = false;
        return false;
    }

    return true;
}

bool MQTTModule::enqueueJob_(uint8_t producerId, uint16_t messageId, uint8_t priority, uint8_t flags)
{
    bool ok = false;
    portENTER_CRITICAL(&jobsMux_);

    int16_t idx = findJobSlot_(producerId, messageId);
    if (idx >= 0) {
        Job& job = jobs_[(uint8_t)idx];
        job.flags |= flags;

        if (priority > job.priority) {
            job.priority = priority;
        }

        if (job.processing) {
            job.requeueAfterProcess = true;
            ok = true;
            portEXIT_CRITICAL(&jobsMux_);
            return ok;
        }

        if (job.queued) {
            if (job.priority > job.queuedPrio) {
                ok = queueSlot_((uint8_t)idx, job.priority, true);
            } else {
                ok = true;
            }
            portEXIT_CRITICAL(&jobsMux_);
            return ok;
        }

        job.retryCount = 0;
        job.notBeforeMs = 0;
        ok = queueSlot_((uint8_t)idx, job.priority, false);
        portEXIT_CRITICAL(&jobsMux_);
        return ok;
    }

    idx = allocJobSlot_();
    if (idx < 0) {
        portEXIT_CRITICAL(&jobsMux_);
        return false;
    }

    Job& job = jobs_[(uint8_t)idx];
    job = Job{};
    job.used = true;
    job.producerId = producerId;
    job.messageId = messageId;
    job.priority = priority;
    job.flags = flags;
    job.retryCount = 0;
    job.notBeforeMs = 0;

    ok = queueSlot_((uint8_t)idx, job.priority, false);
    if (!ok) {
        job = Job{};
    }

    portEXIT_CRITICAL(&jobsMux_);
    return ok;
}

bool MQTTModule::enqueue(uint8_t producerId, uint16_t messageId, MqttPublishPriority priority, uint8_t flags)
{
    if (producerId == 0U) return false;
    return enqueueJob_(producerId, messageId, (uint8_t)priority, flags);
}

bool MQTTModule::dequeueNextJob_(uint32_t nowMs, uint8_t& slotIdx)
{
    static const uint8_t order[3] = {
        (uint8_t)MqttPublishPriority::High,
        (uint8_t)MqttPublishPriority::Normal,
        (uint8_t)MqttPublishPriority::Low
    };

    portENTER_CRITICAL(&jobsMux_);
    const uint16_t maxScan = highQ_.count + normalQ_.count + lowQ_.count + 4U;

    for (uint16_t scan = 0; scan < maxScan; ++scan) {
        for (uint8_t i = 0; i < 3; ++i) {
            JobQueueItem item{};
            if (!queuePop_(order[i], item)) continue;
            if (item.slot >= MaxJobs) continue;

            Job& job = jobs_[item.slot];
            if (!job.used || !job.queued || job.queueToken != item.token || job.queuedPrio != order[i]) {
                continue;
            }

            job.queued = false;
            if (job.processing) continue;

            if ((int32_t)(nowMs - job.notBeforeMs) < 0) {
                (void)queueSlot_(item.slot, job.priority, false);
                continue;
            }

            job.processing = true;
            slotIdx = item.slot;
            portEXIT_CRITICAL(&jobsMux_);
            return true;
        }
    }

    portEXIT_CRITICAL(&jobsMux_);
    return false;
}

bool MQTTModule::tryPublishNow_(const char* topic, const char* payload, uint8_t qos, bool retain)
{
    if (!topic || !payload) return false;
    if (state_ != MQTTState::Connected) return false;
    if (!client_) return false;

    const uint32_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (freeHeap < Limits::NetworkPublish::MinFreeHeapBytes ||
        largest < Limits::NetworkPublish::MinLargestBlockBytes) {
        return false;
    }

    if (qos > 0U) {
        static constexpr int kMaxOutboxBytes = 12 * 1024;
        const int outboxBytes = esp_mqtt_client_get_outbox_size(client_);
        if (outboxBytes >= kMaxOutboxBytes) return false;
    }

    const int packetId = esp_mqtt_client_publish(client_, topic, payload, 0, qos, retain ? 1 : 0);
    return packetId >= 0;
}

void MQTTModule::processJobs_(uint32_t nowMs)
{
    for (uint8_t budget = 0; budget < ProcessBudgetPerTick; ++budget) {
        uint8_t slotIdx = 0;
        if (!dequeueNextJob_(nowMs, slotIdx)) break;

        uint8_t producerId = 0;
        uint16_t messageId = 0;
        {
            portENTER_CRITICAL(&jobsMux_);
            Job& job = jobs_[slotIdx];
            if (!job.used || !job.processing) {
                portEXIT_CRITICAL(&jobsMux_);
                continue;
            }
            producerId = job.producerId;
            messageId = job.messageId;
            portEXIT_CRITICAL(&jobsMux_);
        }

        const MqttPublishProducer* producer = findProducer_(producerId);
        MqttBuildResult buildResult = MqttBuildResult::PermanentError;
        bool published = false;

        memset(topicBuf_, 0, sizeof(topicBuf_));
        memset(payloadBuf_, 0, sizeof(payloadBuf_));

        MqttBuildContext ctx{};
        ctx.topic = topicBuf_;
        ctx.topicCapacity = (uint16_t)sizeof(topicBuf_);
        ctx.payload = payloadBuf_;
        ctx.payloadCapacity = (uint16_t)sizeof(payloadBuf_);

        if (producer && producer->buildMessage) {
            buildResult = producer->buildMessage(producer->ctx, messageId, ctx);
            if (buildResult == MqttBuildResult::Ready) {
                if (ctx.topicLen == 0U && ctx.topic[0] != '\0') {
                    ctx.topicLen = (uint16_t)strnlen(ctx.topic, ctx.topicCapacity);
                }
                if (ctx.payloadLen == 0U && ctx.payload[0] != '\0') {
                    ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
                }

                if (ctx.topicLen == 0U || ctx.payloadLen == 0U) {
                    buildResult = MqttBuildResult::PermanentError;
                } else {
                    published = tryPublishNow_(ctx.topic, ctx.payload, ctx.qos, ctx.retain);
                }
            }
        }

        bool callbackPublished = false;
        bool callbackDeferred = false;
        bool callbackDropped = false;

        portENTER_CRITICAL(&jobsMux_);
        Job& job = jobs_[slotIdx];
        if (!job.used) {
            portEXIT_CRITICAL(&jobsMux_);
            continue;
        }

        if (published) {
            job.processing = false;
            job.retryCount = 0;
            job.notBeforeMs = 0;
            if (job.requeueAfterProcess) {
                job.requeueAfterProcess = false;
                (void)queueSlot_(slotIdx, job.priority, false);
            } else {
                job = Job{};
            }
            callbackPublished = true;
        } else if (buildResult == MqttBuildResult::RetryLater ||
                   (buildResult == MqttBuildResult::Ready && !published)) {
            uint32_t backoff = RetryMinMs;
            if (job.retryCount > 0U) {
                backoff = (uint32_t)RetryMinMs << job.retryCount;
                if (backoff > RetryMaxMs) backoff = RetryMaxMs;
            }
            if (backoff > RetryMaxMs) backoff = RetryMaxMs;

            if (job.retryCount < 15U) ++job.retryCount;
            job.notBeforeMs = nowMs + backoff;
            job.processing = false;
            job.requeueAfterProcess = false;
            (void)queueSlot_(slotIdx, job.priority, false);
            callbackDeferred = true;
        } else {
            job.processing = false;
            if (job.requeueAfterProcess) {
                job.requeueAfterProcess = false;
                job.retryCount = 0;
                job.notBeforeMs = 0;
                (void)queueSlot_(slotIdx, job.priority, false);
                callbackDeferred = true;
            } else {
                job = Job{};
                callbackDropped = true;
            }
        }
        portEXIT_CRITICAL(&jobsMux_);

        if (producer) {
            if (callbackPublished && producer->onMessagePublished) {
                producer->onMessagePublished(producer->ctx, messageId);
            } else if (callbackDeferred && producer->onMessageDeferred) {
                producer->onMessageDeferred(producer->ctx, messageId);
            } else if (callbackDropped && producer->onMessageDropped) {
                producer->onMessageDropped(producer->ctx, messageId);
            }
        }
    }
}

bool MQTTModule::ensureClient_()
{
    if (client_ && !clientConfigDirty_) return true;

    destroyClient_();

    const int uriLen = snprintf(brokerUri_, sizeof(brokerUri_), "mqtt://%s:%ld", cfgData_.host, (long)cfgData_.port);
    if (!(uriLen > 0 && (size_t)uriLen < sizeof(brokerUri_))) return false;

    esp_mqtt_client_config_t cfg = {};
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    cfg.broker.address.uri = brokerUri_;
    cfg.credentials.client_id = deviceId_;
    if (cfgData_.user[0] != '\0') {
        cfg.credentials.username = cfgData_.user;
        cfg.credentials.authentication.password = cfgData_.pass;
    }
    cfg.session.last_will.topic = topicStatus_;
    cfg.session.last_will.msg = "{\"online\":false}";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.network.disable_auto_reconnect = true;
#else
    cfg.uri = brokerUri_;
    cfg.client_id = deviceId_;
    if (cfgData_.user[0] != '\0') {
        cfg.username = cfgData_.user;
        cfg.password = cfgData_.pass;
    }
    cfg.lwt_topic = topicStatus_;
    cfg.lwt_msg = "{\"online\":false}";
    cfg.lwt_qos = 1;
    cfg.lwt_retain = 1;
    cfg.disable_auto_reconnect = true;
    cfg.user_context = this;
#endif

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) return false;

    if (esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MQTTModule::mqttEventHandlerStatic_, this) != ESP_OK) {
        destroyClient_();
        return false;
    }

    clientConfigDirty_ = false;
    return true;
}

void MQTTModule::stopClient_(bool intentional)
{
    if (!client_) return;
    if (intentional) suppressDisconnectEvent_ = true;
    (void)esp_mqtt_client_stop(client_);
    clientStarted_ = false;
}

void MQTTModule::destroyClient_()
{
    if (!client_) return;
    (void)esp_mqtt_client_stop(client_);
    (void)esp_mqtt_client_destroy(client_);
    client_ = nullptr;
    clientStarted_ = false;
}

void MQTTModule::connectMqtt_()
{
    buildTopics_();
    suppressDisconnectEvent_ = false;

    if (!ensureClient_()) {
        setState_(MQTTState::ErrorWait);
        return;
    }

    esp_err_t err = ESP_FAIL;
    if (!clientStarted_) {
        err = esp_mqtt_client_start(client_);
        if (err == ESP_OK) clientStarted_ = true;
    } else {
        err = esp_mqtt_client_reconnect(client_);
    }

    if (err != ESP_OK) {
        setState_(MQTTState::ErrorWait);
        return;
    }

    setState_(MQTTState::Connecting);
}

void MQTTModule::onConnect_(bool)
{
    suppressDisconnectEvent_ = false;

    (void)esp_mqtt_client_subscribe(client_, topicCmd_, 0);
    (void)esp_mqtt_client_subscribe(client_, topicCfgSet_, 1);

    retryCount_ = 0;
    retryDelayMs_ = Limits::Mqtt::Backoff::MinMs;
    setState_(MQTTState::Connected);

    (void)enqueue(ProducerIdStatus, StatusMsgOnline, MqttPublishPriority::High, 0);
    if (cfgProducer_) {
        cfgProducer_->requestFullSync(MqttPublishPriority::Low);
    }
    runtimeProducerCore_.onConnected();
    enqueueAlarmFullSync_();

    const uint32_t now = millis();
    for (uint8_t i = 0; i < runtimePublisherCount_; ++i) {
        RuntimePublisher& p = runtimePublishers_[i];
        if (!p.used || p.periodMs == 0U) continue;
        p.nextDueMs = now;
        (void)enqueue(ProducerIdStatus, p.messageId, MqttPublishPriority::Normal, 0);
    }
}

void MQTTModule::onDisconnect_(const esp_mqtt_error_codes_t*)
{
    if (suppressDisconnectEvent_) {
        suppressDisconnectEvent_ = false;
        return;
    }

    setState_(MQTTState::ErrorWait);
}

void MQTTModule::onMessage_(const char* topic,
                            size_t topicLen,
                            const char* payload,
                            size_t len,
                            size_t index,
                            size_t total)
{
    if (!rxQ_) return;
    if (!topic || !payload) {
        countRxDrop_();
        return;
    }
    if (index != 0 || len != total) {
        countRxDrop_();
        return;
    }

    if (topicLen >= sizeof(RxMsg{}.topic) || len >= sizeof(RxMsg{}.payload)) {
        countOversizeDrop_();
        return;
    }

    RxMsg msg{};
    memcpy(msg.topic, topic, topicLen);
    msg.topic[topicLen] = '\0';
    memcpy(msg.payload, payload, len);
    msg.payload[len] = '\0';

    if (xQueueSend(rxQ_, &msg, 0) != pdTRUE) {
        countRxDrop_();
    }
}

void MQTTModule::mqttEventHandlerStatic_(void* handler_args,
                                         esp_event_base_t,
                                         int32_t event_id,
                                         void* event_data)
{
    MQTTModule* self = static_cast<MQTTModule*>(handler_args);
    if (!self || !event_data) return;

    esp_mqtt_event_handle_t ev = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            self->onConnect_(ev->session_present != 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->onDisconnect_(ev ? ev->error_handle : nullptr);
            break;
        case MQTT_EVENT_DATA:
            self->onMessage_(ev->topic,
                             (size_t)ev->topic_len,
                             ev->data,
                             (size_t)ev->data_len,
                             (size_t)ev->current_data_offset,
                             (size_t)ev->total_data_len);
            break;
        default:
            break;
    }
}

void MQTTModule::formatTopic(char* out, size_t outLen, const char* suffix) const
{
    if (!out || outLen == 0 || !suffix) return;
    snprintf(out, outLen, "%s/%s/%s", cfgData_.baseTopic, deviceId_, suffix);
}

bool MQTTModule::enqueueAck_(const char* topicSuffix,
                             const char* payload,
                             uint8_t qos,
                             bool retain,
                             MqttPublishPriority priority)
{
    if (!topicSuffix || topicSuffix[0] == '\0') return false;
    if (!payload) return false;

    AckMessage& m = ackMessages_[ackWriteCursor_];
    ackWriteCursor_ = (uint8_t)((ackWriteCursor_ + 1U) % MaxAckMessages);

    m = AckMessage{};
    m.used = true;
    m.messageId = ackNextMessageId_;
    m.qos = qos;
    m.retain = retain;
    snprintf(m.topicSuffix, sizeof(m.topicSuffix), "%s", topicSuffix);
    snprintf(m.payload, sizeof(m.payload), "%s", payload);

    ++ackNextMessageId_;
    if (ackNextMessageId_ == 0U) ++ackNextMessageId_;

    if (!enqueue(ProducerIdAck, m.messageId, priority, 0)) {
        m.used = false;
        return false;
    }
    return true;
}

void MQTTModule::processRx_(const RxMsg& msg)
{
    if (strcmp(msg.topic, topicCmd_) == 0) {
        processRxCmd_(msg);
        return;
    }

    if (strcmp(msg.topic, topicCfgSet_) == 0) {
        processRxCfgSet_(msg);
        return;
    }

    publishRxError_(MqttTopics::SuffixAck, ErrorCode::UnknownTopic, "rx", false);
}

void MQTTModule::processRxCmd_(const RxMsg& msg)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    DeserializationError err = deserializeJson(doc, msg.payload);
    if (err || !doc.is<JsonObjectConst>()) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::BadCmdJson, "cmd", true);
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    JsonVariantConst cmdVar = root["cmd"];
    if (!cmdVar.is<const char*>()) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::MissingCmd, "cmd", true);
        return;
    }

    const char* cmdVal = cmdVar.as<const char*>();
    if (!cmdVal || cmdVal[0] == '\0') {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::MissingCmd, "cmd", true);
        return;
    }

    if (!cmdSvc_ || !cmdSvc_->execute) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::CmdServiceUnavailable, "cmd", false);
        return;
    }

    char cmd[Limits::Mqtt::Buffers::CmdName] = {0};
    size_t cmdLen = strlen(cmdVal);
    if (cmdLen >= sizeof(cmd)) cmdLen = sizeof(cmd) - 1U;
    memcpy(cmd, cmdVal, cmdLen);
    cmd[cmdLen] = '\0';

    const char* argsJson = nullptr;
    char argsBuf[Limits::Mqtt::Buffers::CmdArgs] = {0};
    JsonVariantConst argsVar = root["args"];
    if (!argsVar.isNull()) {
        const size_t written = serializeJson(argsVar, argsBuf, sizeof(argsBuf));
        if (written == 0U || written >= sizeof(argsBuf)) {
            publishRxError_(MqttTopics::SuffixAck, ErrorCode::ArgsTooLarge, "cmd", true);
            return;
        }
        argsJson = argsBuf;
    }

    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, cmd, msg.payload, argsJson, replyBuf_, sizeof(replyBuf_));
    if (!ok) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::CmdHandlerFailed, "cmd", false);
        return;
    }

    const int wrote = snprintf(payloadBuf_, sizeof(payloadBuf_),
                               "{\"ok\":true,\"cmd\":\"%s\",\"reply\":%s}",
                               cmd,
                               replyBuf_);
    if (!(wrote > 0 && (size_t)wrote < sizeof(payloadBuf_))) {
        publishRxError_(MqttTopics::SuffixAck, ErrorCode::InternalAckOverflow, "cmd", false);
        return;
    }

    (void)enqueueAck_(MqttTopics::SuffixAck, payloadBuf_, 0, false, MqttPublishPriority::High);
}

void MQTTModule::processRxCfgSet_(const RxMsg& msg)
{
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        publishRxError_(MqttTopics::SuffixCfgAck, ErrorCode::CfgServiceUnavailable, "cfg/set", false);
        return;
    }

    static constexpr size_t CFG_DOC_CAPACITY = Limits::JsonCfgBuf;
    static StaticJsonDocument<CFG_DOC_CAPACITY> cfgDoc;
    cfgDoc.clear();

    const DeserializationError cfgErr = deserializeJson(cfgDoc, msg.payload);
    if (cfgErr || !cfgDoc.is<JsonObjectConst>()) {
        publishRxError_(MqttTopics::SuffixCfgAck, ErrorCode::BadCfgJson, "cfg/set", true);
        return;
    }

    if (!cfgSvc_->applyJson(cfgSvc_->ctx, msg.payload)) {
        publishRxError_(MqttTopics::SuffixCfgAck, ErrorCode::CfgApplyFailed, "cfg/set", false);
        return;
    }

    if (!writeOkJson(payloadBuf_, sizeof(payloadBuf_), "cfg/set")) {
        snprintf(payloadBuf_, sizeof(payloadBuf_), "{\"ok\":true}");
    }
    (void)enqueueAck_(MqttTopics::SuffixCfgAck, payloadBuf_, 1, false, MqttPublishPriority::High);
}

void MQTTModule::publishRxError_(const char* ackTopicSuffix, ErrorCode code, const char* where, bool parseFailure)
{
    if (parseFailure) ++parseFailCount_;
    else ++handlerFailCount_;

    syncRxMetrics_();

    if (!writeErrorJson(payloadBuf_, sizeof(payloadBuf_), code, where)) {
        snprintf(payloadBuf_, sizeof(payloadBuf_), "{\"ok\":false}");
    }

    (void)enqueueAck_(ackTopicSuffix, payloadBuf_, 0, false, MqttPublishPriority::High);
}

void MQTTModule::syncRxMetrics_()
{
    if (!dataStore_) return;
    setMqttRxDrop(*dataStore_, rxDropCount_);
    setMqttOversizeDrop(*dataStore_, oversizeDropCount_);
    setMqttParseFail(*dataStore_, parseFailCount_);
    setMqttHandlerFail(*dataStore_, handlerFailCount_);
}

void MQTTModule::countRxDrop_()
{
    ++rxDropCount_;
    syncRxMetrics_();
}

void MQTTModule::countOversizeDrop_()
{
    ++oversizeDropCount_;
    ++rxDropCount_;
    syncRxMetrics_();
}

void MQTTModule::enqueueAlarmFullSync_()
{
    if (!alarmSvc_ || !alarmSvc_->listIds) return;

    AlarmId ids[Limits::Alarm::MaxAlarms]{};
    const uint8_t n = alarmSvc_->listIds(alarmSvc_->ctx, ids, (uint8_t)Limits::Alarm::MaxAlarms);
    for (uint8_t i = 0; i < n; ++i) {
        const uint16_t msg = (uint16_t)(AlarmMsgStateBase + (uint16_t)ids[i]);
        (void)enqueue(ProducerIdAlarm, msg, MqttPublishPriority::High, 0);
    }
    (void)enqueue(ProducerIdAlarm, AlarmMsgMeta, MqttPublishPriority::Normal, 0);
    (void)enqueue(ProducerIdAlarm, AlarmMsgPack, MqttPublishPriority::Normal, 0);
}

MqttBuildResult MQTTModule::buildAckStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->buildAck_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

void MQTTModule::onAckPublishedStatic_(void* ctx, uint16_t messageId)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    if (self) self->onAckPublished_(messageId);
}

void MQTTModule::onAckDroppedStatic_(void* ctx, uint16_t messageId)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    if (self) self->onAckDropped_(messageId);
}

MqttBuildResult MQTTModule::buildStatusStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->buildStatus_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult MQTTModule::buildAlarmStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->buildAlarm_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

MqttBuildResult MQTTModule::buildAck_(uint16_t messageId, MqttBuildContext& ctx)
{
    for (uint8_t i = 0; i < MaxAckMessages; ++i) {
        const AckMessage& msg = ackMessages_[i];
        if (!msg.used || msg.messageId != messageId) continue;

        const int tw = snprintf(ctx.topic, ctx.topicCapacity, "%s/%s/%s", cfgData_.baseTopic, deviceId_, msg.topicSuffix);
        if (!(tw > 0 && (uint16_t)tw < ctx.topicCapacity)) return MqttBuildResult::PermanentError;

        const int pw = snprintf(ctx.payload, ctx.payloadCapacity, "%s", msg.payload);
        if (!(pw > 0 && (uint16_t)pw < ctx.payloadCapacity)) return MqttBuildResult::PermanentError;

        ctx.topicLen = (uint16_t)tw;
        ctx.payloadLen = (uint16_t)pw;
        ctx.qos = msg.qos;
        ctx.retain = msg.retain;
        return MqttBuildResult::Ready;
    }
    return MqttBuildResult::NoLongerNeeded;
}

void MQTTModule::onAckPublished_(uint16_t messageId)
{
    for (uint8_t i = 0; i < MaxAckMessages; ++i) {
        AckMessage& msg = ackMessages_[i];
        if (msg.used && msg.messageId == messageId) {
            msg.used = false;
            break;
        }
    }
}

void MQTTModule::onAckDropped_(uint16_t messageId)
{
    onAckPublished_(messageId);
}

MqttBuildResult MQTTModule::buildStatus_(uint16_t messageId, MqttBuildContext& ctx)
{
    if (messageId == StatusMsgOnline) {
        const int tw = snprintf(ctx.topic, ctx.topicCapacity, "%s", topicStatus_);
        if (!(tw > 0 && (uint16_t)tw < ctx.topicCapacity)) return MqttBuildResult::PermanentError;

        static const char kOnlinePayload[] = "{\"online\":true}";
        const int pw = snprintf(ctx.payload, ctx.payloadCapacity, "%s", kOnlinePayload);
        if (!(pw > 0 && (uint16_t)pw < ctx.payloadCapacity)) return MqttBuildResult::PermanentError;

        ctx.topicLen = (uint16_t)tw;
        ctx.payloadLen = (uint16_t)pw;
        ctx.qos = 1;
        ctx.retain = true;
        return MqttBuildResult::Ready;
    }

    if (messageId >= StatusMsgRuntimeBase) {
        const uint8_t idx = (uint8_t)(messageId - StatusMsgRuntimeBase);
        if (idx >= runtimePublisherCount_) return MqttBuildResult::NoLongerNeeded;

        RuntimePublisher& p = runtimePublishers_[idx];
        if (!p.used || !p.topic || !p.build) return MqttBuildResult::NoLongerNeeded;

        const int tw = snprintf(ctx.topic, ctx.topicCapacity, "%s", p.topic);
        if (!(tw > 0 && (uint16_t)tw < ctx.topicCapacity)) return MqttBuildResult::PermanentError;

        const bool ok = p.build(this, ctx.payload, ctx.payloadCapacity);
        if (!ok) {
            return p.allowNoPayload ? MqttBuildResult::NoLongerNeeded : MqttBuildResult::RetryLater;
        }

        ctx.topicLen = (uint16_t)tw;
        ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
        ctx.qos = p.qos;
        ctx.retain = p.retain;
        return MqttBuildResult::Ready;
    }

    return MqttBuildResult::NoLongerNeeded;
}

MqttBuildResult MQTTModule::buildAlarm_(uint16_t messageId, MqttBuildContext& ctx)
{
    if (!alarmSvc_) return MqttBuildResult::RetryLater;

    if (messageId == AlarmMsgMeta) {
        if (!alarmSvc_->activeCount || !alarmSvc_->highestSeverity) return MqttBuildResult::PermanentError;

        const int tw = snprintf(ctx.topic, ctx.topicCapacity, "%s/%s/rt/alarms/m", cfgData_.baseTopic, deviceId_);
        if (!(tw > 0 && (uint16_t)tw < ctx.topicCapacity)) return MqttBuildResult::PermanentError;

        const uint8_t active = alarmSvc_->activeCount(alarmSvc_->ctx);
        const AlarmSeverity highest = alarmSvc_->highestSeverity(alarmSvc_->ctx);
        const int pw = snprintf(ctx.payload,
                                ctx.payloadCapacity,
                                "{\"a\":%u,\"h\":%u,\"ts\":%lu}",
                                (unsigned)active,
                                (unsigned)((uint8_t)highest),
                                (unsigned long)millis());
        if (!(pw > 0 && (uint16_t)pw < ctx.payloadCapacity)) return MqttBuildResult::PermanentError;

        ctx.topicLen = (uint16_t)tw;
        ctx.payloadLen = (uint16_t)pw;
        ctx.qos = 0;
        ctx.retain = false;
        return MqttBuildResult::Ready;
    }

    if (messageId == AlarmMsgPack) {
        if (!alarmSvc_->buildPacked) return MqttBuildResult::PermanentError;

        const int tw = snprintf(ctx.topic, ctx.topicCapacity, "%s/%s/rt/alarms/p", cfgData_.baseTopic, deviceId_);
        if (!(tw > 0 && (uint16_t)tw < ctx.topicCapacity)) return MqttBuildResult::PermanentError;

        if (!alarmSvc_->buildPacked(alarmSvc_->ctx, ctx.payload, ctx.payloadCapacity, 8)) {
            return MqttBuildResult::RetryLater;
        }

        ctx.topicLen = (uint16_t)tw;
        ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
        ctx.qos = 0;
        ctx.retain = false;
        return MqttBuildResult::Ready;
    }

    if (messageId >= AlarmMsgStateBase) {
        if (!alarmSvc_->buildAlarmState) return MqttBuildResult::PermanentError;

        const AlarmId id = (AlarmId)(messageId - AlarmMsgStateBase);

        const int tw = snprintf(ctx.topic,
                                ctx.topicCapacity,
                                "%s/%s/rt/alarms/id%u",
                                cfgData_.baseTopic,
                                deviceId_,
                                (unsigned)((uint16_t)id));
        if (!(tw > 0 && (uint16_t)tw < ctx.topicCapacity)) return MqttBuildResult::PermanentError;

        if (!alarmSvc_->buildAlarmState(alarmSvc_->ctx, id, ctx.payload, ctx.payloadCapacity)) {
            return MqttBuildResult::RetryLater;
        }

        ctx.topicLen = (uint16_t)tw;
        ctx.payloadLen = (uint16_t)strnlen(ctx.payload, ctx.payloadCapacity);
        ctx.qos = 0;
        ctx.retain = false;
        return MqttBuildResult::Ready;
    }

    return MqttBuildResult::NoLongerNeeded;
}

void MQTTModule::onEventStatic_(const Event& e, void* user)
{
    MQTTModule* self = static_cast<MQTTModule*>(user);
    if (self) self->onEvent_(e);
}

void MQTTModule::onEvent_(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (!p) return;

        if (p->id == DATAKEY_WIFI_READY) {
            if (!dataStore_) return;

            const bool ready = wifiReady(*dataStore_);
            if (ready == netReady_) return;

            netReady_ = ready;
            netReadyTs_ = millis();

            if (netReady_) {
                if (state_ != MQTTState::Connected) setState_(MQTTState::WaitingNetwork);
            } else {
                stopClient_(true);
                setState_(MQTTState::WaitingNetwork);
            }
            return;
        }

        runtimeProducerCore_.onDataChanged(p->id);
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (!p) return;

        const char* key = p->nvsKey;
        if (key && key[0] != '\0' && isMqttConnKey(key)) {
            clientConfigDirty_ = true;
            stopClient_(true);
            netReadyTs_ = millis();
            setState_(MQTTState::WaitingNetwork);
        }

        return;
    }

    if (e.id == EventId::AlarmRaised ||
        e.id == EventId::AlarmCleared ||
        e.id == EventId::AlarmAcked ||
        e.id == EventId::AlarmSilenceChanged ||
        e.id == EventId::AlarmConditionChanged) {
        const AlarmPayload* p = (const AlarmPayload*)e.payload;
        if (p && e.len >= sizeof(AlarmPayload)) {
            (void)enqueue(ProducerIdAlarm,
                          (uint16_t)(AlarmMsgStateBase + (uint16_t)p->alarmId),
                          MqttPublishPriority::High,
                          0);
        } else {
            enqueueAlarmFullSync_();
            return;
        }

        (void)enqueue(ProducerIdAlarm, AlarmMsgMeta, MqttPublishPriority::Normal, 0);
        (void)enqueue(ProducerIdAlarm, AlarmMsgPack, MqttPublishPriority::Normal, 0);
        return;
    }
}

void MQTTModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Mqtt;
    constexpr uint8_t kCfgBranchId = kMqttCfgBranch;
    cfg.registerVar(hostVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(portVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(userVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(passVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(baseTopicVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);

    wifiSvc_ = services.get<WifiService>("wifi");
    cmdSvc_ = services.get<CommandService>("cmd");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    timeSchedSvc_ = services.get<TimeSchedulerService>("time.scheduler");
    alarmSvc_ = services.get<AlarmService>("alarms");
    logHub_ = services.get<LogHubService>("loghub");
    (void)logHub_;

    const EventBusService* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    rxDropCount_ = 0;
    parseFailCount_ = 0;
    handlerFailCount_ = 0;
    oversizeDropCount_ = 0;
    syncRxMetrics_();

    mqttSvc_.enqueue = MQTTModule::svcEnqueue_;
    mqttSvc_.registerProducer = MQTTModule::svcRegisterProducer_;
    mqttSvc_.formatTopic = MQTTModule::svcFormatTopic_;
    mqttSvc_.isConnected = MQTTModule::svcIsConnected_;
    mqttSvc_.ctx = this;
    (void)services.add("mqtt", &mqttSvc_);

    if (!cfgProducer_) {
        cfgProducer_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgProducer_) {
        cfgProducer_->configure(this,
                                ProducerIdConfig,
                                kMqttCfgRoutes,
                                (uint8_t)(sizeof(kMqttCfgRoutes) / sizeof(kMqttCfgRoutes[0])),
                                services);
    }
    runtimeProducerCore_.configure(&mqttSvc_);

    ackProducerDesc_ = MqttPublishProducer{};
    ackProducerDesc_.producerId = ProducerIdAck;
    ackProducerDesc_.ctx = this;
    ackProducerDesc_.buildMessage = MQTTModule::buildAckStatic_;
    ackProducerDesc_.onMessagePublished = MQTTModule::onAckPublishedStatic_;
    ackProducerDesc_.onMessageDropped = MQTTModule::onAckDroppedStatic_;

    statusProducerDesc_ = MqttPublishProducer{};
    statusProducerDesc_.producerId = ProducerIdStatus;
    statusProducerDesc_.ctx = this;
    statusProducerDesc_.buildMessage = MQTTModule::buildStatusStatic_;

    runtimeProducerDesc_ = MqttPublishProducer{};
    runtimeProducerDesc_.producerId = ProducerIdRuntime;
    runtimeProducerDesc_.ctx = &runtimeProducerCore_;
    runtimeProducerDesc_.buildMessage = [](void* ctx, uint16_t messageId, MqttBuildContext& buildCtx) -> MqttBuildResult {
        RuntimeProducer* p = static_cast<RuntimeProducer*>(ctx);
        return p ? p->buildMessage(messageId, buildCtx) : MqttBuildResult::PermanentError;
    };
    runtimeProducerDesc_.onMessagePublished = [](void* ctx, uint16_t messageId) {
        RuntimeProducer* p = static_cast<RuntimeProducer*>(ctx);
        if (p) p->onMessagePublished(messageId);
    };
    runtimeProducerDesc_.onMessageDropped = [](void* ctx, uint16_t messageId) {
        RuntimeProducer* p = static_cast<RuntimeProducer*>(ctx);
        if (p) p->onMessageDropped(messageId);
    };

    alarmProducerDesc_ = MqttPublishProducer{};
    alarmProducerDesc_.producerId = ProducerIdAlarm;
    alarmProducerDesc_.ctx = this;
    alarmProducerDesc_.buildMessage = MQTTModule::buildAlarmStatic_;

    (void)registerProducer(&ackProducerDesc_);
    (void)registerProducer(&statusProducerDesc_);
    (void)registerProducer(&runtimeProducerDesc_);
    (void)registerProducer(&alarmProducerDesc_);

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmRaised, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmCleared, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmAcked, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmSilenceChanged, &MQTTModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmConditionChanged, &MQTTModule::onEventStatic_, this);
    }

    makeDeviceId(deviceId_, sizeof(deviceId_));
    buildTopics_();

    const size_t rxItemSize = sizeof(RxMsg);
    const size_t rxQueueStorageLen = ((size_t)Limits::Mqtt::Capacity::RxQueueLen) * rxItemSize;
    rxQueueStorage_ = static_cast<uint8_t*>(heap_caps_malloc(rxQueueStorageLen, MALLOC_CAP_8BIT));
    if (rxQueueStorage_) {
        rxQ_ = xQueueCreateStatic(Limits::Mqtt::Capacity::RxQueueLen,
                                  rxItemSize,
                                  rxQueueStorage_,
                                  &rxQueueStruct_);
    } else {
        rxQ_ = nullptr;
    }

    netReady_ = dataStore_ ? wifiReady(*dataStore_) : false;
    netReadyTs_ = millis();
    retryCount_ = 0;
    retryDelayMs_ = Limits::Mqtt::Backoff::MinMs;

    setState_(cfgData_.enabled ? MQTTState::WaitingNetwork : MQTTState::Disabled);
}

void MQTTModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgProducer_) {
        cfgProducer_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgProducer_) {
        cfgProducer_->configure(this,
                                ProducerIdConfig,
                                kMqttCfgRoutes,
                                (uint8_t)(sizeof(kMqttCfgRoutes) / sizeof(kMqttCfgRoutes[0])),
                                services);
    }
    runtimeProducerCore_.configure(&mqttSvc_);
    runtimeProducerCore_.rebuildRoutes();
}

void MQTTModule::loop()
{
    if (!cfgData_.enabled) {
        if (state_ != MQTTState::Disabled) {
            stopClient_(true);
            setState_(MQTTState::Disabled);
        }
        vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::DisabledDelayMs));
        return;
    }

    switch (state_) {
        case MQTTState::Disabled:
            setState_(MQTTState::WaitingNetwork);
            break;

        case MQTTState::WaitingNetwork:
            if (!startupReady_) break;
            if (!netReady_) break;
            if ((millis() - netReadyTs_) >= Limits::Mqtt::Timing::NetWarmupMs) {
                connectMqtt_();
            }
            break;

        case MQTTState::Connecting:
            if ((millis() - stateTs_) > Limits::Mqtt::Timing::ConnectTimeoutMs) {
                stopClient_(true);
                setState_(MQTTState::ErrorWait);
            }
            break;

        case MQTTState::Connected: {
            if (rxQ_) {
                RxMsg msg{};
                while (xQueueReceive(rxQ_, &msg, 0) == pdTRUE) {
                    processRx_(msg);
                }
            }

            const uint32_t now = millis();
            for (uint8_t i = 0; i < runtimePublisherCount_; ++i) {
                RuntimePublisher& p = runtimePublishers_[i];
                if (!p.used || p.periodMs == 0U) continue;
                if ((int32_t)(now - p.nextDueMs) < 0) continue;

                p.nextDueMs = now + p.periodMs;
                (void)enqueue(ProducerIdStatus, p.messageId, MqttPublishPriority::Normal, 0);
            }

            processJobs_(now);
            break;
        }

        case MQTTState::ErrorWait:
            if (!netReady_) {
                setState_(MQTTState::WaitingNetwork);
                break;
            }

            if ((millis() - stateTs_) >= retryDelayMs_) {
                ++retryCount_;
                uint32_t next = retryDelayMs_;
                if (next < Limits::Mqtt::Backoff::Step1Ms) next = Limits::Mqtt::Backoff::Step1Ms;
                else if (next < Limits::Mqtt::Backoff::Step2Ms) next = Limits::Mqtt::Backoff::Step2Ms;
                else if (next < Limits::Mqtt::Backoff::Step3Ms) next = Limits::Mqtt::Backoff::Step3Ms;
                else if (next < Limits::Mqtt::Backoff::Step4Ms) next = Limits::Mqtt::Backoff::Step4Ms;
                else next = Limits::Mqtt::Backoff::MaxMs;

                next = clampU32(next, Limits::Mqtt::Backoff::MinMs, Limits::Mqtt::Backoff::MaxMs);
                retryDelayMs_ = jitterMs(next, Limits::Mqtt::Backoff::JitterPct);
                setState_(MQTTState::WaitingNetwork);
            }
            break;
    }

    vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::LoopDelayMs));
}
