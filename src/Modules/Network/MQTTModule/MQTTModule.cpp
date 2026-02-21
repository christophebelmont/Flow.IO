/**
 * @file MQTTModule.cpp
 * @brief Implementation file.
 */
#include "MQTTModule.h"
#include "Core/Runtime.h"
#include "Core/MqttTopics.h"
#include "Core/ConfigBranchIds.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <initializer_list>
#include "Core/EventBus/EventPayloads.h"
#define LOG_TAG "MqttModu"
#include "Core/ModuleLog.h"

static uint32_t clampU32(uint32_t v, uint32_t minV, uint32_t maxV) {
    if (v < minV) return minV;
    if (v > maxV) return maxV;
    return v;
}

static uint32_t jitterMs(uint32_t baseMs, uint8_t pct) {
    if (baseMs == 0 || pct == 0) return baseMs;
    uint32_t span = (baseMs * pct) / 100U;
    uint32_t r = esp_random();
    uint32_t delta = r % (2U * span + 1U);
    int32_t signedDelta = (int32_t)delta - (int32_t)span;
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

bool MQTTModule::svcPublish(void* ctx, const char* topic, const char* payload, int qos, bool retain)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->publish(topic, payload, qos, retain) : false;
}

void MQTTModule::svcFormatTopic(void* ctx, const char* suffix, char* out, size_t outLen)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    if (!self) return;
    self->formatTopic(out, outLen, suffix);
}

bool MQTTModule::svcIsConnected(void* ctx)
{
    MQTTModule* self = static_cast<MQTTModule*>(ctx);
    return self ? self->isConnected() : false;
}

void MQTTModule::setState(MQTTState s) {
    state = s;
    stateTs = millis();
    if (dataStore) {
        setMqttReady(*dataStore, s == MQTTState::Connected);
    }
}

static void makeDeviceId(char* out, size_t len) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void MQTTModule::buildTopics() {
    snprintf(topicCmd, sizeof(topicCmd), "%s/%s/%s", cfgData.baseTopic, deviceId, MqttTopics::SuffixCmd);
    snprintf(topicAck, sizeof(topicAck), "%s/%s/%s", cfgData.baseTopic, deviceId, MqttTopics::SuffixAck);
    snprintf(topicStatus, sizeof(topicStatus), "%s/%s/%s", cfgData.baseTopic, deviceId, MqttTopics::SuffixStatus);
    snprintf(topicCfgSet, sizeof(topicCfgSet), "%s/%s/%s", cfgData.baseTopic, deviceId, MqttTopics::SuffixCfgSet);
    snprintf(topicCfgAck, sizeof(topicCfgAck), "%s/%s/%s", cfgData.baseTopic, deviceId, MqttTopics::SuffixCfgAck);
    snprintf(topicRtAlarmsMeta, sizeof(topicRtAlarmsMeta), "%s/%s/rt/alarms/m", cfgData.baseTopic, deviceId);
    snprintf(topicRtAlarmsPack, sizeof(topicRtAlarmsPack), "%s/%s/rt/alarms/p", cfgData.baseTopic, deviceId);
}

bool MQTTModule::ensureClient_()
{
    if (client_ && !clientConfigDirty_) return true;

    destroyClient_();

    const int uriLen = snprintf(brokerUri_, sizeof(brokerUri_), "mqtt://%s:%ld", cfgData.host, (long)cfgData.port);
    if (!(uriLen > 0 && (size_t)uriLen < sizeof(brokerUri_))) {
        LOGE("MQTT broker URI overflow");
        return false;
    }

    esp_mqtt_client_config_t cfg = {};
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    cfg.broker.address.uri = brokerUri_;
    cfg.credentials.client_id = deviceId;
    if (cfgData.user[0] != '\0') {
        cfg.credentials.username = cfgData.user;
        cfg.credentials.authentication.password = cfgData.pass;
    }
    cfg.session.last_will.topic = topicStatus;
    cfg.session.last_will.msg = "{\"online\":false}";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.network.disable_auto_reconnect = true;
#else
    cfg.uri = brokerUri_;
    cfg.client_id = deviceId;
    if (cfgData.user[0] != '\0') {
        cfg.username = cfgData.user;
        cfg.password = cfgData.pass;
    }
    cfg.lwt_topic = topicStatus;
    cfg.lwt_msg = "{\"online\":false}";
    cfg.lwt_qos = 1;
    cfg.lwt_retain = 1;
    cfg.disable_auto_reconnect = true;
    cfg.user_context = this;
#endif

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        LOGE("esp_mqtt_client_init failed");
        return false;
    }

    if (esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &MQTTModule::mqttEventHandlerStatic_, this) != ESP_OK) {
        LOGE("esp_mqtt_client_register_event failed");
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

void MQTTModule::connectMqtt() {
    buildTopics();
    suppressDisconnectEvent_ = false;
    if (!ensureClient_()) {
        setState(MQTTState::ErrorWait);
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
        LOGW("MQTT connect request failed err=%d", (int)err);
        setState(MQTTState::ErrorWait);
        return;
    }

    setState(MQTTState::Connecting);
    LOGI("Connecting to %s:%ld", cfgData.host, cfgData.port);
}

void MQTTModule::onConnect_(bool) {
    suppressDisconnectEvent_ = false;
    LOGI("Connected subscribe %s", topicCmd);
    (void)esp_mqtt_client_subscribe(client_, topicCmd, 0);
    (void)esp_mqtt_client_subscribe(client_, topicCfgSet, 1);

    _retryCount = 0;
    _retryDelayMs = Limits::Mqtt::Backoff::MinMs;
    setState(MQTTState::Connected);

    (void)publish(topicStatus, "{\"online\":true}", 1, true);

    if (sensorsTopic && sensorsBuild) {
        sensorsPending = true;
        sensorsPendingDirtyMask = (DIRTY_SENSORS | DIRTY_ACTUATORS);
        sensorsActiveDirtyMask = 0;
        lastSensorsPublishMs = 0;
    }

    // Defer cfg/* publication to loop() so actuator runtime snapshots can be
    // flushed first and avoid stale HA state immediately after reconnect.
    _pendingPublish = true;
    alarmsMetaPending_ = true;
    alarmsFullSyncPending_ = true;
    alarmsPackPending_ = true;
}

void MQTTModule::onDisconnect_() {
    if (suppressDisconnectEvent_) {
        suppressDisconnectEvent_ = false;
        return;
    }
    LOGW("Disconnected");
    cfgRampActive_ = false;
    cfgRampRestartRequested_ = false;
    cfgRampIndex_ = 0;
    setState(MQTTState::ErrorWait);
}

void MQTTModule::onMessage_(const char* topic, size_t topicLen,
                            const char* payload, size_t len, size_t index, size_t total) {
    if (!rxQ) return;
    if (!topic || !payload) {
        countRxDrop_();
        return;
    }
    if (index != 0 || len != total) {
        countRxDrop_();
        return;
    }

    const size_t topicCap = sizeof(RxMsg{}.topic);
    const size_t payloadCap = sizeof(RxMsg{}.payload);
    if (topicLen >= topicCap || len >= payloadCap) {
        countOversizeDrop_();
        return;
    }

    RxMsg m{};
    memcpy(m.topic, topic, topicLen);
    m.topic[topicLen] = '\0';

    memcpy(m.payload, payload, len);
    size_t copyLen = len;
    m.payload[copyLen] = '\0';

    if (xQueueSend(rxQ, &m, 0) != pdTRUE) {
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
            self->onDisconnect_();
            break;
        case MQTT_EVENT_DATA:
            self->onMessage_(ev->topic,
                             (size_t)ev->topic_len,
                             ev->data,
                             (size_t)ev->data_len,
                             (size_t)ev->current_data_offset,
                             (size_t)ev->total_data_len);
            break;
        case MQTT_EVENT_ERROR:
            if (ev->error_handle) {
                LOGW("MQTT error type=%d tls_last=%d tls_stack=%d conn_refused=%d sock_errno=%d",
                     (int)ev->error_handle->error_type,
                     (int)ev->error_handle->esp_tls_last_esp_err,
                     (int)ev->error_handle->esp_tls_stack_err,
                     (int)ev->error_handle->connect_return_code,
                     (int)ev->error_handle->esp_transport_sock_errno);
            } else {
                LOGW("MQTT error event without details");
            }
            break;
        default:
            break;
    }
}

void MQTTModule::refreshConfigModules()
{
    if (cfgSvc && cfgSvc->listModules) {
        cfgModuleCount = cfgSvc->listModules(cfgSvc->ctx, cfgModules, Limits::Mqtt::Capacity::CfgTopicMax);
        if (cfgModuleCount >= Limits::Mqtt::Capacity::CfgTopicMax) {
            LOGW("Config module list reached limit (%u), some cfg/* blocks may be omitted", (unsigned)Limits::Mqtt::Capacity::CfgTopicMax);
        }
    } else {
        cfgModuleCount = 0;
    }
}

void MQTTModule::publishConfigBlocks(bool retained) {
    if (!cfgSvc || !cfgSvc->toJsonModule) return;
    refreshConfigModules();
    for (size_t i = 0; i < cfgModuleCount; ++i) {
        (void)publishConfigModuleAt(i, retained);
    }
}

void MQTTModule::enqueueCfgBranch_(uint16_t branchId)
{
    if (branchId == (uint16_t)ConfigBranchId::Unknown) return;

    portENTER_CRITICAL(&pendingCfgMux_);
    for (uint8_t i = 0; i < pendingCfgBranchCount_; ++i) {
        if (pendingCfgBranches_[i] == branchId) {
            portEXIT_CRITICAL(&pendingCfgMux_);
            return;
        }
    }
    if (pendingCfgBranchCount_ < PendingCfgBranchesMax) {
        pendingCfgBranches_[pendingCfgBranchCount_++] = branchId;
    } else {
        // Queue saturated: fallback to full cfg ramp to avoid dropping updates.
        _pendingPublish = true;
    }
    portEXIT_CRITICAL(&pendingCfgMux_);
}

uint8_t MQTTModule::takePendingCfgBranches_(uint16_t* out, uint8_t maxItems)
{
    if (!out || maxItems == 0) return 0;

    portENTER_CRITICAL(&pendingCfgMux_);
    const uint8_t n = (pendingCfgBranchCount_ < maxItems) ? pendingCfgBranchCount_ : maxItems;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = pendingCfgBranches_[i];
    }
    pendingCfgBranchCount_ = 0;
    portEXIT_CRITICAL(&pendingCfgMux_);
    return n;
}

void MQTTModule::processPendingCfgBranches_()
{
    uint16_t branchIds[PendingCfgBranchesMax] = {0};
    const uint8_t n = takePendingCfgBranches_(branchIds, PendingCfgBranchesMax);
    for (uint8_t i = 0; i < n; ++i) {
        const char* module = configBranchModuleName((ConfigBranchId)branchIds[i]);
        if (!module || module[0] == '\0' || !publishConfigModuleByName_(module, true)) {
            _pendingPublish = true;
        }
    }
}

void MQTTModule::beginConfigRamp_(uint32_t nowMs)
{
    if (!cfgSvc || !cfgSvc->toJsonModule) {
        cfgRampActive_ = false;
        cfgRampRestartRequested_ = false;
        cfgRampIndex_ = 0;
        return;
    }

    refreshConfigModules();
    cfgRampIndex_ = 0;
    cfgRampNextMs_ = nowMs;
    cfgRampRestartRequested_ = false;
    cfgRampActive_ = (cfgModuleCount > 0);
}

void MQTTModule::runConfigRamp_(uint32_t nowMs)
{
    if (!cfgRampActive_) return;
    if (state != MQTTState::Connected) {
        cfgRampActive_ = false;
        cfgRampRestartRequested_ = false;
        cfgRampIndex_ = 0;
        return;
    }

    if (cfgRampRestartRequested_) {
        beginConfigRamp_(nowMs);
    }

    if ((int32_t)(nowMs - cfgRampNextMs_) < 0) return;
    if (cfgRampIndex_ >= cfgModuleCount) {
        cfgRampActive_ = false;
        return;
    }

    (void)publishConfigModuleAt(cfgRampIndex_, true);
    ++cfgRampIndex_;
    cfgRampNextMs_ = nowMs + Limits::Mqtt::Timing::CfgRampStepMs;

    if (cfgRampIndex_ >= cfgModuleCount) {
        cfgRampActive_ = false;
    }
}

bool MQTTModule::publishConfigModuleAt(size_t idx, bool retained)
{
    if (!cfgSvc || !cfgSvc->toJsonModule) return false;
    if (idx >= cfgModuleCount) return false;
    if (!cfgModules[idx] || cfgModules[idx][0] == '\0') return false;

    char moduleTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    if (!buildCfgTopic_(cfgModules[idx], moduleTopic, sizeof(moduleTopic))) {
        return false;
    }

    if (strcmp(cfgModules[idx], "time/scheduler") == 0) {
        publishTimeSchedulerSlots(retained, moduleTopic);
        return true;
    }

    bool truncated = false;
    bool any = cfgSvc->toJsonModule(cfgSvc->ctx, cfgModules[idx], stateCfgBuf, sizeof(stateCfgBuf), &truncated);
    if (truncated) {
        LOGW("cfg/%s truncated (buffer=%u)", cfgModules[idx], (unsigned)sizeof(stateCfgBuf));
        // Avoid publishing malformed partial JSON when truncation happens.
        if (!writeErrorJson(stateCfgBuf, sizeof(stateCfgBuf), ErrorCode::CfgTruncated, "cfg")) {
            snprintf(stateCfgBuf, sizeof(stateCfgBuf), "{\"ok\":false}");
        }
        if (!publish(moduleTopic, stateCfgBuf, 1, retained)) {
            if (lowHeapSinceMs_ != 0U) LOGD("cfg/%s publish deferred (truncated payload)", cfgModules[idx]);
            else LOGW("cfg/%s publish failed (truncated payload)", cfgModules[idx]);
            return false;
        }
        return true;
    }
    if (!any) return false;
    if (!publish(moduleTopic, stateCfgBuf, 1, retained)) {
        if (lowHeapSinceMs_ != 0U) LOGD("cfg/%s publish deferred", cfgModules[idx]);
        else LOGW("cfg/%s publish failed", cfgModules[idx]);
        return false;
    }
    return true;
}

bool MQTTModule::publishConfigBlocksFromPatch(const char* patchJson, bool retained)
{
    if (!patchJson || patchJson[0] == '\0') return false;
    if (!cfgSvc || !cfgSvc->toJsonModule) return false;
    static constexpr size_t PATCH_DOC_CAPACITY = Limits::JsonPatchBuf;
    static StaticJsonDocument<PATCH_DOC_CAPACITY> patchDoc;
    patchDoc.clear();
    const DeserializationError patchErr = deserializeJson(patchDoc, patchJson);
    if (patchErr || !patchDoc.is<JsonObjectConst>()) return false;
    JsonObjectConst patch = patchDoc.as<JsonObjectConst>();

    refreshConfigModules();
    bool publishedAny = false;
    for (size_t i = 0; i < cfgModuleCount; ++i) {
        const char* module = cfgModules[i];
        if (!module || module[0] == '\0') continue;
        if (!patch.containsKey(module)) continue;
        publishedAny = publishConfigModuleAt(i, retained) || publishedAny;
    }
    return publishedAny;
}

bool MQTTModule::publishConfigModuleByName_(const char* module, bool retained)
{
    if (!module || module[0] == '\0') return false;
    if (!cfgSvc || !cfgSvc->toJsonModule) return false;

    char moduleTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    if (!buildCfgTopic_(module, moduleTopic, sizeof(moduleTopic))) {
        return false;
    }

    if (strcmp(module, "time/scheduler") == 0) {
        publishTimeSchedulerSlots(retained, moduleTopic);
        return true;
    }

    bool truncated = false;
    const bool any = cfgSvc->toJsonModule(cfgSvc->ctx, module, stateCfgBuf, sizeof(stateCfgBuf), &truncated);
    if (truncated) {
        LOGW("cfg/%s truncated (buffer=%u)", module, (unsigned)sizeof(stateCfgBuf));
        if (!writeErrorJson(stateCfgBuf, sizeof(stateCfgBuf), ErrorCode::CfgTruncated, "cfg")) {
            snprintf(stateCfgBuf, sizeof(stateCfgBuf), "{\"ok\":false}");
        }
        if (!publish(moduleTopic, stateCfgBuf, 1, retained)) {
            if (lowHeapSinceMs_ != 0U) LOGD("cfg/%s publish deferred (truncated payload)", module);
            else LOGW("cfg/%s publish failed (truncated payload)", module);
            return false;
        }
        return true;
    }
    if (!any) return false;

    if (!publish(moduleTopic, stateCfgBuf, 1, retained)) {
        if (lowHeapSinceMs_ != 0U) LOGD("cfg/%s publish deferred", module);
        else LOGW("cfg/%s publish failed", module);
        return false;
    }
    return true;
}

bool MQTTModule::buildCfgTopic_(const char* module, char* out, size_t outLen) const
{
    if (!module || module[0] == '\0' || !out || outLen == 0) return false;
    const int tw = snprintf(out, outLen, "%s/%s/cfg/%s", cfgData.baseTopic, deviceId, module);
    if (!(tw > 0 && (size_t)tw < outLen)) {
        LOGW("cfg publish: topic truncated for module=%s", module);
        return false;
    }
    return true;
}

void MQTTModule::publishTimeSchedulerSlots(bool retained, const char* rootTopic)
{
    if (!rootTopic || rootTopic[0] == '\0') return;

    // Root topic stays small and indicates that details are split by slot.
    if (!publish(rootTopic, "{\"mode\":\"per_slot\",\"slots\":16}", 1, retained)) {
        if (lowHeapSinceMs_ != 0U) LOGD("cfg/time/scheduler root publish deferred (mqtt pressure)");
        else LOGW("cfg/time/scheduler root publish failed");
    }

    if (!timeSchedSvc || !timeSchedSvc->getSlot) {
        LOGW("time.scheduler service unavailable for cfg publication");
        return;
    }

    char slotTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    for (uint8_t slot = 0; slot < TIME_SCHED_MAX_SLOTS; ++slot) {
        snprintf(slotTopic, sizeof(slotTopic), "%s/slot%u", rootTopic, (unsigned)slot);

        TimeSchedulerSlot def{};
        if (!timeSchedSvc->getSlot(timeSchedSvc->ctx, slot, &def)) {
            snprintf(stateCfgBuf, sizeof(stateCfgBuf),
                     "{\"slot\":%u,\"used\":false}",
                     (unsigned)slot);
            if (!publish(slotTopic, stateCfgBuf, 1, retained)) {
                LOGD("cfg/time/scheduler slot%u publish deferred (unused)", (unsigned)slot);
            }
            continue;
        }

        const char* mode = (def.mode == TimeSchedulerMode::OneShotEpoch) ? "one_shot_epoch" : "recurring_clock";
        snprintf(stateCfgBuf, sizeof(stateCfgBuf),
                 "{\"slot\":%u,\"used\":true,\"event_id\":%u,\"label\":\"%s\",\"enabled\":%s,"
                 "\"mode\":\"%s\",\"has_end\":%s,\"replay_on_boot\":%s,"
                 "\"weekday_mask\":%u,"
                 "\"start\":{\"hour\":%u,\"minute\":%u,\"epoch\":%llu},"
                 "\"end\":{\"hour\":%u,\"minute\":%u,\"epoch\":%llu}}",
                 (unsigned)def.slot,
                 (unsigned)def.eventId,
                 def.label,
                 def.enabled ? "true" : "false",
                 mode,
                 def.hasEnd ? "true" : "false",
                 def.replayStartOnBoot ? "true" : "false",
                 (unsigned)def.weekdayMask,
                 (unsigned)def.startHour,
                 (unsigned)def.startMinute,
                 (unsigned long long)def.startEpochSec,
                 (unsigned)def.endHour,
                 (unsigned)def.endMinute,
                 (unsigned long long)def.endEpochSec);
        if (!publish(slotTopic, stateCfgBuf, 1, retained)) {
            if (lowHeapSinceMs_ != 0U) LOGD("cfg/time/scheduler slot%u publish deferred", (unsigned)slot);
            else LOGW("cfg/time/scheduler slot%u publish failed", (unsigned)slot);
        }
    }
}

bool MQTTModule::publish(const char* topic, const char* payload, int qos, bool retain)
{
    if (!topic || !payload) return false;
    if (state != MQTTState::Connected) return false;
    if (!client_) return false;

    constexpr uint32_t kMinFreeHeapForPublish = 8192U;
    constexpr uint32_t kMinLargestBlockForPublish = 4096U;
    const uint32_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (freeHeap < kMinFreeHeapForPublish || largest < kMinLargestBlockForPublish) {
        const uint32_t now = millis();
        if (lowHeapSinceMs_ == 0U) lowHeapSinceMs_ = now;
        const uint32_t lowForMs = now - lowHeapSinceMs_;
        if (lowForMs >= 10000U) {
            if ((now - lastLowHeapWarnMs_) >= 10000U) {
                lastLowHeapWarnMs_ = now;
                LOGW("skip mqtt publish (low heap sustained %lums free=%lu largest=%lu need_free=%lu need_largest=%lu topic=%s)",
                     (unsigned long)lowForMs,
                     (unsigned long)freeHeap,
                     (unsigned long)largest,
                     (unsigned long)kMinFreeHeapForPublish,
                     (unsigned long)kMinLargestBlockForPublish,
                     topic);
            }
        } else {
            if ((now - lastLowHeapWarnMs_) >= 5000U) {
                lastLowHeapWarnMs_ = now;
                LOGD("skip mqtt publish (transient low heap free=%lu largest=%lu topic=%s)",
                     (unsigned long)freeHeap,
                     (unsigned long)largest,
                     topic);
            }
        }
        return false;
    }
    lowHeapSinceMs_ = 0U;

    // Guard against QoS outbox buildup (retained cfg/* bursts, link hiccups),
    // which can fragment heap and starve other modules.
    if (qos > 0) {
        constexpr int kMaxOutboxBytes = 12 * 1024;
        const int outboxBytes = esp_mqtt_client_get_outbox_size(client_);
        if (outboxBytes >= kMaxOutboxBytes) {
            const uint32_t now = millis();
            if ((now - lastOutboxWarnMs_) >= 2000U) {
                lastOutboxWarnMs_ = now;
                LOGW("skip qos%d publish (outbox=%dB limit=%dB topic=%s)",
                     qos, outboxBytes, kMaxOutboxBytes, topic);
            }
            return false;
        }
    }

    const int packetId = esp_mqtt_client_publish(client_, topic, payload, 0, qos, retain ? 1 : 0);
    if (packetId < 0) {
        LOGW("mqtt publish rejected topic=%s qos=%d retain=%d", topic, qos, retain ? 1 : 0);
        return false;
    }
    if (qos > 0) {
        const size_t payloadLen = strlen(payload);
        LOGD("MQTT TX ok id=%d qos=%d retain=%d len=%u",
             packetId,
             qos,
             retain ? 1 : 0,
             (unsigned)payloadLen);
    }
    return true;
}

bool MQTTModule::publishAlarmState_(AlarmId id)
{
    if (!alarmSvc || !alarmSvc->buildAlarmState) return false;
    if (!alarmSvc->buildAlarmState(alarmSvc->ctx, id, publishBuf, sizeof(publishBuf))) {
        LOGW("alarm state build failed id=%u (buffer=%u)", (unsigned)((uint16_t)id), (unsigned)sizeof(publishBuf));
        return false;
    }

    char alarmTopic[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    const int tw = snprintf(
        alarmTopic,
        sizeof(alarmTopic),
        "%s/%s/rt/alarms/id%u",
        cfgData.baseTopic,
        deviceId,
        (unsigned)((uint16_t)id));
    if (!(tw > 0 && (size_t)tw < sizeof(alarmTopic))) {
        LOGW("alarm topic truncated id=%u", (unsigned)((uint16_t)id));
        return false;
    }
    return publish(alarmTopic, publishBuf, 0, false);
}

bool MQTTModule::publishAlarmMeta_()
{
    if (!alarmSvc || !alarmSvc->activeCount || !alarmSvc->highestSeverity) return false;
    if (topicRtAlarmsMeta[0] == '\0') return false;

    const uint8_t active = alarmSvc->activeCount(alarmSvc->ctx);
    const AlarmSeverity highest = alarmSvc->highestSeverity(alarmSvc->ctx);
    const int wrote = snprintf(
        publishBuf,
        sizeof(publishBuf),
        "{\"a\":%u,\"h\":%u,\"ts\":%lu}",
        (unsigned)active,
        (unsigned)((uint8_t)highest),
        (unsigned long)millis());
    if (!(wrote > 0 && (size_t)wrote < sizeof(publishBuf))) {
        LOGW("alarm meta payload truncated (buffer=%u)", (unsigned)sizeof(publishBuf));
        return false;
    }
    return publish(topicRtAlarmsMeta, publishBuf, 0, false);
}

bool MQTTModule::publishAlarmPack_()
{
    if (!alarmSvc || !alarmSvc->buildPacked) return false;
    if (topicRtAlarmsPack[0] == '\0') return false;
    if (!alarmSvc->buildPacked(alarmSvc->ctx, publishBuf, sizeof(publishBuf), 8)) {
        LOGW("alarm pack build failed (buffer=%u)", (unsigned)sizeof(publishBuf));
        return false;
    }
    return publish(topicRtAlarmsPack, publishBuf, 0, false);
}

void MQTTModule::enqueuePendingAlarmId_(AlarmId id)
{
    if (id == AlarmId::None) return;
    portENTER_CRITICAL(&pendingAlarmMux_);
    for (uint8_t i = 0; i < pendingAlarmCount_; ++i) {
        if (pendingAlarmIds_[i] == id) {
            portEXIT_CRITICAL(&pendingAlarmMux_);
            return;
        }
    }
    if (pendingAlarmCount_ < PendingAlarmIdsMax) {
        pendingAlarmIds_[pendingAlarmCount_++] = id;
    } else {
        alarmsFullSyncPending_ = true;
    }
    portEXIT_CRITICAL(&pendingAlarmMux_);
}

uint8_t MQTTModule::takePendingAlarmIds_(AlarmId* out, uint8_t maxItems)
{
    if (!out || maxItems == 0) return 0;

    portENTER_CRITICAL(&pendingAlarmMux_);
    const uint8_t n = (pendingAlarmCount_ < maxItems) ? pendingAlarmCount_ : maxItems;
    for (uint8_t i = 0; i < n; ++i) {
        out[i] = pendingAlarmIds_[i];
    }
    pendingAlarmCount_ = 0;
    portEXIT_CRITICAL(&pendingAlarmMux_);
    return n;
}

void MQTTModule::formatTopic(char* out, size_t outLen, const char* suffix) const
{
    if (!out || outLen == 0 || !suffix) return;
    snprintf(out, outLen, "%s/%s/%s", cfgData.baseTopic, deviceId, suffix);
}

bool MQTTModule::addRuntimePublisher(const char* topic, uint32_t periodMs, int qos, bool retain,
                                     bool (*build)(MQTTModule* self, char* out, size_t outLen))
{
    if (!topic || !build) return false;
    if (publisherCount >= Limits::Mqtt::Capacity::MaxPublishers) return false;
    RuntimePublisher& p = publishers[publisherCount++];
    p.topic = topic;
    p.periodMs = periodMs;
    p.qos = qos;
    p.retain = retain;
    p.build = build;
    p.lastMs = 0;
    return true;
}
void MQTTModule::processRx(const RxMsg& msg) {
    if (strcmp(msg.topic, topicCmd) == 0) return processRxCmd_(msg);
    if (strcmp(msg.topic, topicCfgSet) == 0) return processRxCfgSet_(msg);
    publishRxError_(topicAck, ErrorCode::UnknownTopic, "rx", false);
}

void MQTTModule::processRxCmd_(const RxMsg& msg)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;
    doc.clear();
    DeserializationError err = deserializeJson(doc, msg.payload);
    if (err || !doc.is<JsonObjectConst>()) {
        LOGW("processRxCmd: bad cmd json (topic=%s, payload=%s)", msg.topic, msg.payload);
        publishRxError_(topicAck, ErrorCode::BadCmdJson, "cmd", true);
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    JsonVariantConst cmdVar = root["cmd"];
    if (!cmdVar.is<const char*>()) {
        LOGW("processRxCmd: missing cmd field");
        publishRxError_(topicAck, ErrorCode::MissingCmd, "cmd", true);
        return;
    }
    const char* cmdVal = cmdVar.as<const char*>();
    if (!cmdVal || cmdVal[0] == '\0') {
        LOGW("processRxCmd: empty cmd value");
        publishRxError_(topicAck, ErrorCode::MissingCmd, "cmd", true);
        return;
    }
    if (!cmdSvc || !cmdSvc->execute) {
        LOGW("processRxCmd: command service unavailable (cmd=%s)", cmdVal);
        publishRxError_(topicAck, ErrorCode::CmdServiceUnavailable, "cmd", false);
        return;
    }

    char cmd[Limits::Mqtt::Buffers::CmdName];
    size_t clen = strlen(cmdVal);
    if (clen >= sizeof(cmd)) clen = sizeof(cmd) - 1;
    memcpy(cmd, cmdVal, clen);
    cmd[clen] = '\0';

    const char* argsJson = nullptr;
    char argsBuf[Limits::Mqtt::Buffers::CmdArgs] = {0};
    JsonVariantConst argsVar = root["args"];
    if (!argsVar.isNull()) {
        const size_t written = serializeJson(argsVar, argsBuf, sizeof(argsBuf));
        if (written == 0 || written >= sizeof(argsBuf)) {
            LOGW("processRxCmd: args too large (cmd=%s)", cmd);
            publishRxError_(topicAck, ErrorCode::ArgsTooLarge, "cmd", true);
            return;
        }
        argsJson = argsBuf;
    }

    bool ok = cmdSvc->execute(cmdSvc->ctx, cmd, msg.payload, argsJson, replyBuf, sizeof(replyBuf));
    if (!ok) {
        LOGW("processRxCmd: command handler failed (cmd=%s)", cmd);
        publishRxError_(topicAck, ErrorCode::CmdHandlerFailed, "cmd", false);
        return;
    }

    int wrote = snprintf(ackBuf, sizeof(ackBuf), "{\"ok\":true,\"cmd\":\"%s\",\"reply\":%s}", cmd, replyBuf);
    if (!(wrote > 0 && (size_t)wrote < sizeof(ackBuf))) {
        LOGW("processRxCmd: ack overflow (cmd=%s, wrote=%d)", cmd, wrote);
        publishRxError_(topicAck, ErrorCode::InternalAckOverflow, "cmd", false);
        return;
    }
    if (!publish(topicAck, ackBuf, 0, false)) {
        LOGW("cmd ack publish failed cmd=%s", cmd);
    }

    // cfg/* publication path is intentionally ConfigChanged-only.
}

void MQTTModule::processRxCfgSet_(const RxMsg& msg)
{
    if (!cfgSvc || !cfgSvc->applyJson) {
        publishRxError_(topicCfgAck, ErrorCode::CfgServiceUnavailable, "cfg/set", false);
        return;
    }

    static constexpr size_t CFG_DOC_CAPACITY = Limits::JsonCfgBuf;
    static StaticJsonDocument<CFG_DOC_CAPACITY> cfgDoc;
    cfgDoc.clear();
    const DeserializationError cfgErr = deserializeJson(cfgDoc, msg.payload);
    if (cfgErr || !cfgDoc.is<JsonObjectConst>()) {
        publishRxError_(topicCfgAck, ErrorCode::BadCfgJson, "cfg/set", true);
        return;
    }

    bool ok = cfgSvc->applyJson(cfgSvc->ctx, msg.payload);
    if (!ok) {
        publishRxError_(topicCfgAck, ErrorCode::CfgApplyFailed, "cfg/set", false);
        return;
    }
    // cfg/* publication path is intentionally ConfigChanged-only.

    if (!writeOkJson(ackBuf, sizeof(ackBuf), "cfg/set")) {
        snprintf(ackBuf, sizeof(ackBuf), "{\"ok\":true}");
    }
    if (!publish(topicCfgAck, ackBuf, 1, false)) {
        LOGW("cfg/set ack publish failed");
    }
}

void MQTTModule::publishRxError_(const char* ackTopic, ErrorCode code, const char* where, bool parseFailure)
{
    if (!ackTopic || ackTopic[0] == '\0') return;
    if (parseFailure) ++parseFailCount_;
    else ++handlerFailCount_;
    syncRxMetrics_();

    if (!writeErrorJson(ackBuf, sizeof(ackBuf), code, where)) {
        if (!writeErrorJson(ackBuf, sizeof(ackBuf), ErrorCode::InternalAckOverflow, "rx")) {
            snprintf(ackBuf, sizeof(ackBuf), "{\"ok\":false}");
        }
    }
    if (!publish(ackTopic, ackBuf, 0, false)) {
        LOGW("rx error ack publish failed topic=%s", ackTopic);
    }
}

void MQTTModule::syncRxMetrics_()
{
    if (!dataStore) return;
    setMqttRxDrop(*dataStore, rxDropCount_);
    setMqttOversizeDrop(*dataStore, oversizeDropCount_);
    setMqttParseFail(*dataStore, parseFailCount_);
    setMqttHandlerFail(*dataStore, handlerFailCount_);
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

void MQTTModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Mqtt;
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::Mqtt;
    cfg.registerVar(hostVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(portVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(userVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(passVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(baseTopicVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(enabledVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sensorMinVar, kCfgModuleId, kCfgBranchId);

    wifiSvc = services.get<WifiService>("wifi");
    cmdSvc = services.get<CommandService>("cmd");
    cfgSvc = services.get<ConfigStoreService>("config");
    timeSchedSvc = services.get<TimeSchedulerService>("time.scheduler");
    alarmSvc = services.get<AlarmService>("alarms");
    logHub = services.get<LogHubService>("loghub");

    auto* ebSvc = services.get<EventBusService>("eventbus");
    eventBus = ebSvc ? ebSvc->bus : nullptr;

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore = dsSvc ? dsSvc->store : nullptr;
    rxDropCount_ = 0;
    oversizeDropCount_ = 0;
    parseFailCount_ = 0;
    handlerFailCount_ = 0;
    pendingAlarmCount_ = 0;
    alarmsMetaPending_ = false;
    alarmsFullSyncPending_ = false;
    alarmsPackPending_ = false;
    syncRxMetrics_();

    mqttSvc.publish = MQTTModule::svcPublish;
    mqttSvc.formatTopic = MQTTModule::svcFormatTopic;
    mqttSvc.isConnected = MQTTModule::svcIsConnected;
    mqttSvc.ctx = this;
    services.add("mqtt", &mqttSvc);

    if (eventBus) {
        eventBus->subscribe(EventId::DataChanged, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::DataSnapshotAvailable, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::ConfigChanged, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::AlarmRaised, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::AlarmCleared, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::AlarmAcked, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::AlarmSilenceChanged, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::AlarmConditionChanged, &MQTTModule::onEventStatic, this);
    }

    makeDeviceId(deviceId, sizeof(deviceId));
    buildTopics();
    clientConfigDirty_ = true;
    suppressDisconnectEvent_ = false;

    rxQ = xQueueCreate(Limits::Mqtt::Capacity::RxQueueLen, sizeof(RxMsg));

    refreshConfigModules();

    LOGI("Init id=%s topic=%s cfgModules=%u", deviceId, topicCmd, (unsigned)cfgModuleCount);
    if (timeSchedSvc) {
        LOGI("Time scheduler config will be published per-slot on cfg/time/scheduler/slotN");
    }

    _netReady = dataStore ? wifiReady(*dataStore) : false;
    _netReadyTs = millis();
    _retryCount = 0;
    _retryDelayMs = Limits::Mqtt::Backoff::MinMs;

    setState(cfgData.enabled ? MQTTState::WaitingNetwork : MQTTState::Disabled);
}

void MQTTModule::loop() {
    if (!cfgData.enabled) {
        if (state != MQTTState::Disabled) {
            stopClient_(true);
            setState(MQTTState::Disabled);
        }
        vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::DisabledDelayMs));
        return;
    }

    switch (state) {
    case MQTTState::Disabled: setState(MQTTState::WaitingNetwork); break;
    case MQTTState::WaitingNetwork:
        if (!_startupReady) break;
        if (!_netReady) break;
        if (millis() - _netReadyTs >= Limits::Mqtt::Timing::NetWarmupMs) connectMqtt();
        break;
    case MQTTState::Connecting:
        if (millis() - stateTs > Limits::Mqtt::Timing::ConnectTimeoutMs) {
            LOGW("Connect timeout");
            stopClient_(true);
            setState(MQTTState::ErrorWait);
        }
        break;
    case MQTTState::Connected: {
        RxMsg m;
        while (xQueueReceive(rxQ, &m, 0) == pdTRUE) processRx(m);
        uint32_t now = millis();

        if (alarmsFullSyncPending_ && alarmSvc && alarmSvc->listIds) {
            AlarmId ids[Limits::Alarm::MaxAlarms]{};
            const uint8_t n = alarmSvc->listIds(alarmSvc->ctx, ids, (uint8_t)Limits::Alarm::MaxAlarms);
            bool okAll = true;
            for (uint8_t i = 0; i < n; ++i) {
                if (!publishAlarmState_(ids[i])) okAll = false;
            }
            if (!publishAlarmMeta_()) okAll = false;
            if (okAll) {
                alarmsFullSyncPending_ = false;
                alarmsMetaPending_ = false;
            }
        } else {
            AlarmId pendingIds[PendingAlarmIdsMax]{};
            const uint8_t nPending = takePendingAlarmIds_(pendingIds, PendingAlarmIdsMax);
            for (uint8_t i = 0; i < nPending; ++i) {
                if (!publishAlarmState_(pendingIds[i])) {
                    enqueuePendingAlarmId_(pendingIds[i]);
                }
            }
            if (alarmsMetaPending_) {
                if (publishAlarmMeta_()) {
                    alarmsMetaPending_ = false;
                }
            }
        }
        if (alarmsPackPending_) {
            if (publishAlarmPack_()) {
                alarmsPackPending_ = false;
            }
        }
        if (sensorsPending && sensorsTopic && sensorsBuild) {
            const uint32_t relevantMask = (DIRTY_SENSORS | DIRTY_ACTUATORS);
            if ((sensorsPendingDirtyMask & relevantMask) == 0U) {
                sensorsPendingDirtyMask = relevantMask;
            }
            uint32_t minMs = cfgData.sensorMinPublishMs;
            uint32_t elapsed = (uint32_t)(now - lastSensorsPublishMs);
            bool withinThrottle = (minMs != 0U) && (elapsed < minMs);
            bool hasActuators = (sensorsPendingDirtyMask & DIRTY_ACTUATORS) != 0U;
            bool hasSensors = (sensorsPendingDirtyMask & DIRTY_SENSORS) != 0U;

            if (hasActuators && withinThrottle) {
                sensorsActiveDirtyMask = DIRTY_ACTUATORS;
                if (sensorsBuild(this, publishBuf, sizeof(publishBuf))) {
                    publish(sensorsTopic, publishBuf, 0, false);
                }
                sensorsActiveDirtyMask = 0;
                const bool keepActuatorRetries = ((uint32_t)(now - stateTs) < Limits::Mqtt::Timing::StartupActuatorRetryMs);
                if (!keepActuatorRetries) {
                    sensorsPendingDirtyMask &= ~DIRTY_ACTUATORS;
                }
                if (!hasSensors && !keepActuatorRetries) {
                    sensorsPending = false;
                    sensorsPendingDirtyMask = 0;
                }
            } else if (!withinThrottle || minMs == 0U) {
                sensorsActiveDirtyMask = sensorsPendingDirtyMask & relevantMask;
                if (sensorsActiveDirtyMask == 0U) sensorsActiveDirtyMask = relevantMask;
                if (sensorsBuild(this, publishBuf, sizeof(publishBuf))) {
                    publish(sensorsTopic, publishBuf, 0, false);
                }
                sensorsActiveDirtyMask = 0;
                lastSensorsPublishMs = now;
                sensorsPending = false;
                sensorsPendingDirtyMask = 0;
            }
        }
        if (_pendingPublish) {
            _pendingPublish = false;
            if (cfgRampActive_) cfgRampRestartRequested_ = true;
            else beginConfigRamp_(now);
        }
        processPendingCfgBranches_();
        runConfigRamp_(now);
        for (uint8_t i = 0; i < publisherCount; ++i) {
            RuntimePublisher& p = publishers[i];
            if (!p.topic || !p.build) continue;
            if (p.periodMs == 0) continue;
            if ((uint32_t)(now - p.lastMs) < p.periodMs) continue;
            if (p.build(this, publishBuf, sizeof(publishBuf))) {
                publish(p.topic, publishBuf, p.qos, p.retain);
                p.lastMs = now;
            } else {
                LOGW("runtime snapshot build failed topic=%s (buffer=%u)", p.topic, (unsigned)sizeof(publishBuf));
            }
        }
        break;
    }
    case MQTTState::ErrorWait:
        if (!_netReady) {
            setState(MQTTState::WaitingNetwork);
            break;
        }
        if (millis() - stateTs >= _retryDelayMs) {
            _retryCount++;
            uint32_t next = _retryDelayMs;

            if      (next < Limits::Mqtt::Backoff::Step1Ms)   next = Limits::Mqtt::Backoff::Step1Ms;
            else if (next < Limits::Mqtt::Backoff::Step2Ms)   next = Limits::Mqtt::Backoff::Step2Ms;
            else if (next < Limits::Mqtt::Backoff::Step3Ms)   next = Limits::Mqtt::Backoff::Step3Ms;
            else if (next < Limits::Mqtt::Backoff::Step4Ms)   next = Limits::Mqtt::Backoff::Step4Ms;
            else                                               next = Limits::Mqtt::Backoff::MaxMs;

            next = clampU32(next, Limits::Mqtt::Backoff::MinMs, Limits::Mqtt::Backoff::MaxMs);
            _retryDelayMs = jitterMs(next, Limits::Mqtt::Backoff::JitterPct);
            setState(MQTTState::WaitingNetwork);
        }
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(Limits::Mqtt::Timing::LoopDelayMs));
}

void MQTTModule::onEventStatic(const Event& e, void* user)
{
    static_cast<MQTTModule*>(user)->onEvent(e);
}

void MQTTModule::onEvent(const Event& e)
{
    if (e.id == EventId::DataChanged) {
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (!p) return;
        if (p->id != DATAKEY_WIFI_READY) return;
        if (!dataStore) return;

        bool ready = wifiReady(*dataStore);
        if (ready == _netReady) return;

        _netReady = ready;
        _netReadyTs = millis();

        if (_netReady) {
            LOGI("DataStore networkReady=true -> warmup");
            if (state != MQTTState::Connected) setState(MQTTState::WaitingNetwork);
        } else {
            LOGI("DataStore networkReady=false -> disconnect and wait");
            stopClient_(true);
            setState(MQTTState::WaitingNetwork);
        }
        return;
    }

    if (e.id == EventId::DataSnapshotAvailable) {
        const DataSnapshotPayload* p = (const DataSnapshotPayload*)e.payload;
        if (!p) return;
        uint32_t relevant = p->dirtyFlags & (DIRTY_SENSORS | DIRTY_ACTUATORS);
        if (relevant == 0U) return;
        sensorsPending = true;
        sensorsPendingDirtyMask |= relevant;
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (!p) return;

        const char* key = p->nvsKey;
        if (!key || key[0] == '\0') return;

        if (isMqttConnKey(key)) {
            LOGI("MQTT config changed (%s) -> reconnect", key);
            clientConfigDirty_ = true;
            stopClient_(true);
            _netReadyTs = millis();
            setState(MQTTState::WaitingNetwork);
        }

        const uint16_t branchId = p->branchId;
        if (branchId == (uint16_t)ConfigBranchId::Unknown) {
            _pendingPublish = true;
            return;
        }
        enqueueCfgBranch_(branchId);
        if (branchId == (uint16_t)ConfigBranchId::Time) {
            // Scheduler JSON shape depends on week/day policy carried by cfg/time.
            enqueueCfgBranch_((uint16_t)ConfigBranchId::TimeScheduler);
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
            enqueuePendingAlarmId_((AlarmId)p->alarmId);
        } else {
            alarmsFullSyncPending_ = true;
        }
        alarmsMetaPending_ = true;
        alarmsPackPending_ = true;
        return;
    }
}
