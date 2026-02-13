/**
 * @file MQTTModule.cpp
 * @brief Implementation file.
 */
#include "MQTTModule.h"
#include "Core/Runtime.h"
#include "Core/SystemStats.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
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
    snprintf(topicCmd, sizeof(topicCmd), "%s/%s/cmd", cfgData.baseTopic, deviceId);
    snprintf(topicAck, sizeof(topicAck), "%s/%s/ack", cfgData.baseTopic, deviceId);
    snprintf(topicStatus, sizeof(topicStatus), "%s/%s/status", cfgData.baseTopic, deviceId);
    snprintf(topicCfgSet, sizeof(topicCfgSet), "%s/%s/cfg/set", cfgData.baseTopic, deviceId);
    snprintf(topicCfgAck, sizeof(topicCfgAck), "%s/%s/cfg/ack", cfgData.baseTopic, deviceId);
    for (size_t i = 0; i < cfgModuleCount; ++i) {
        snprintf(topicCfgBlocks[i], sizeof(topicCfgBlocks[i]),
                 "%s/%s/cfg/%s", cfgData.baseTopic, deviceId, cfgModules[i]);
    }
}

void MQTTModule::connectMqtt() {
    buildTopics();
    client.setServer(cfgData.host, (uint16_t)cfgData.port);
    if (cfgData.user[0] != '\0') client.setCredentials(cfgData.user, cfgData.pass);
    client.setWill(topicStatus, 1, true, "{\"online\":false}");
    client.connect();
    setState(MQTTState::Connecting);
    LOGI("Connecting to %s:%ld", cfgData.host, cfgData.port);
}

void MQTTModule::onConnect(bool) {
    LOGI("Connected subscribe %s", topicCmd);
    client.subscribe(topicCmd, 0);
    client.subscribe(topicCfgSet, 1);
    client.publish(topicStatus, 1, true, "{\"online\":true}");
    publishConfigBlocks(true);

    _retryCount = 0;
    _retryDelayMs = 2000;
    setState(MQTTState::Connected);

    if (sensorsTopic && sensorsBuild) {
        sensorsPending = true;
        sensorsPendingDirtyMask = (DIRTY_SENSORS | DIRTY_ACTUATORS);
        sensorsActiveDirtyMask = 0;
        lastSensorsPublishMs = 0;
    }
}

void MQTTModule::onDisconnect(AsyncMqttClientDisconnectReason) {
    LOGW("Disconnected");
    setState(MQTTState::ErrorWait);
}

void MQTTModule::onMessage(char* topic, char* payload, AsyncMqttClientMessageProperties,
                           size_t len, size_t, size_t total) {
    if (!rxQ) return;
    if (len != total) return;

    RxMsg m{};
    strncpy(m.topic, topic, sizeof(m.topic) - 1);
    size_t copyLen = len;
    if (copyLen >= sizeof(m.payload)) copyLen = sizeof(m.payload) - 1;
    memcpy(m.payload, payload, copyLen);
    m.payload[copyLen] = '\0';

    xQueueSend(rxQ, &m, 0);
}

void MQTTModule::refreshConfigModules()
{
    if (cfgSvc && cfgSvc->listModules) {
        cfgModuleCount = cfgSvc->listModules(cfgSvc->ctx, cfgModules, CFG_TOPIC_MAX);
        if (cfgModuleCount >= CFG_TOPIC_MAX) {
            LOGW("Config module list reached limit (%u), some cfg/* blocks may be omitted", (unsigned)CFG_TOPIC_MAX);
        }
    } else {
        cfgModuleCount = 0;
    }
    for (size_t i = 0; i < cfgModuleCount; ++i) {
        snprintf(topicCfgBlocks[i], sizeof(topicCfgBlocks[i]),
                 "%s/%s/cfg/%s", cfgData.baseTopic, deviceId, cfgModules[i]);
    }
}

void MQTTModule::publishConfigBlocks(bool retained) {
    if (!cfgSvc || !cfgSvc->toJsonModule) return;
    refreshConfigModules();
    for (size_t i = 0; i < cfgModuleCount; ++i) {
        (void)publishConfigModuleAt(i, retained);
    }
}

bool MQTTModule::publishConfigModuleAt(size_t idx, bool retained)
{
    if (!cfgSvc || !cfgSvc->toJsonModule) return false;
    if (idx >= cfgModuleCount) return false;
    if (!cfgModules[idx] || cfgModules[idx][0] == '\0') return false;

    if (strcmp(cfgModules[idx], "time/scheduler") == 0) {
        publishTimeSchedulerSlots(retained, topicCfgBlocks[idx]);
        return true;
    }

    bool truncated = false;
    bool any = cfgSvc->toJsonModule(cfgSvc->ctx, cfgModules[idx], stateCfgBuf, sizeof(stateCfgBuf), &truncated);
    if (truncated) {
        LOGW("cfg/%s truncated (buffer=%u)", cfgModules[idx], (unsigned)sizeof(stateCfgBuf));
        // Avoid publishing malformed partial JSON when truncation happens.
        const char* truncPayload = "{\"ok\":false,\"err\":\"cfg_truncated\"}";
        client.publish(topicCfgBlocks[idx], 1, retained, truncPayload);
        return true;
    }
    if (!any) return false;
    client.publish(topicCfgBlocks[idx], 1, retained, stateCfgBuf);
    return true;
}

bool MQTTModule::publishConfigBlocksFromPatch(const char* patchJson, bool retained)
{
    if (!patchJson || patchJson[0] == '\0') return false;
    if (!cfgSvc || !cfgSvc->toJsonModule) return false;
    static constexpr size_t PATCH_DOC_CAPACITY = 1024;
    StaticJsonDocument<PATCH_DOC_CAPACITY> patchDoc;
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

void MQTTModule::publishTimeSchedulerSlots(bool retained, const char* rootTopic)
{
    if (!rootTopic || rootTopic[0] == '\0') return;

    // Root topic stays small and indicates that details are split by slot.
    client.publish(rootTopic, 1, retained, "{\"mode\":\"per_slot\",\"slots\":16}");

    if (!timeSchedSvc || !timeSchedSvc->getSlot) {
        LOGW("time.scheduler service unavailable for cfg publication");
        return;
    }

    char slotTopic[160] = {0};
    for (uint8_t slot = 0; slot < TIME_SCHED_MAX_SLOTS; ++slot) {
        snprintf(slotTopic, sizeof(slotTopic), "%s/slot%u", rootTopic, (unsigned)slot);

        TimeSchedulerSlot def{};
        if (!timeSchedSvc->getSlot(timeSchedSvc->ctx, slot, &def)) {
            snprintf(stateCfgBuf, sizeof(stateCfgBuf),
                     "{\"slot\":%u,\"used\":false}",
                     (unsigned)slot);
            client.publish(slotTopic, 1, retained, stateCfgBuf);
            continue;
        }

        const char* mode = (def.mode == TimeSchedulerMode::OneShotEpoch) ? "one_shot_epoch" : "recurring_clock";
        snprintf(stateCfgBuf, sizeof(stateCfgBuf),
                 "{\"slot\":%u,\"used\":true,\"event_id\":%u,\"label\":\"%s\",\"enabled\":%s,"
                 "\"mode\":\"%s\",\"has_end\":%s,\"replay_start_on_boot\":%s,"
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
        client.publish(slotTopic, 1, retained, stateCfgBuf);
    }
}

bool MQTTModule::publish(const char* topic, const char* payload, int qos, bool retain)
{
    if (!topic || !payload) return false;
    if (state != MQTTState::Connected) return false;
    client.publish(topic, qos, retain, payload);
    return true;
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
    if (publisherCount >= MAX_PUBLISHERS) return false;
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
    if (strcmp(msg.topic, topicCmd) == 0) {
        static constexpr size_t CMD_DOC_CAPACITY = 1024;
        StaticJsonDocument<CMD_DOC_CAPACITY> doc;
        DeserializationError err = deserializeJson(doc, msg.payload);
        if (err || !doc.is<JsonObjectConst>()) {
            client.publish(topicAck, 0, false, "{\"ok\":false,\"err\":\"bad_cmd_json\"}");
            return;
        }
        JsonObjectConst root = doc.as<JsonObjectConst>();
        JsonVariantConst cmdVar = root["cmd"];
        if (!cmdVar.is<const char*>()) {
            client.publish(topicAck, 0, false, "{\"ok\":false,\"err\":\"missing_cmd\"}");
            return;
        }
        const char* cmdVal = cmdVar.as<const char*>();
        if (!cmdVal || cmdVal[0] == '\0') {
            client.publish(topicAck, 0, false, "{\"ok\":false,\"err\":\"missing_cmd\"}");
            return;
        }
        if (!cmdSvc) {
            client.publish(topicAck, 0, false, "{\"ok\":false,\"err\":\"cmd_service_unavailable\"}");
            return;
        }

        char cmd[64];
        size_t clen = strlen(cmdVal);
        if (clen >= sizeof(cmd)) clen = sizeof(cmd) - 1;
        memcpy(cmd, cmdVal, clen); cmd[clen] = '\0';

        const char* argsJson = nullptr;
        char argsBuf[320] = {0};
        JsonVariantConst argsVar = root["args"];
        if (!argsVar.isNull()) {
            const size_t written = serializeJson(argsVar, argsBuf, sizeof(argsBuf));
            if (written == 0 || written >= sizeof(argsBuf)) {
                client.publish(topicAck, 0, false, "{\"ok\":false,\"err\":\"args_too_large\"}");
                return;
            }
            argsJson = argsBuf;
        }

        bool ok = cmdSvc->execute(cmdSvc->ctx, cmd, msg.payload, argsJson, replyBuf, sizeof(replyBuf));
        snprintf(ackBuf, sizeof(ackBuf), "{\"ok\":%s,\"cmd\":\"%s\",\"reply\":%s}",
                 ok ? "true" : "false", cmd, replyBuf);
        client.publish(topicAck, 0, false, ackBuf);
        return;
    }

    if (strcmp(msg.topic, topicCfgSet) == 0) {
        if (!cfgSvc || !cfgSvc->applyJson) return;
        bool ok = cfgSvc->applyJson(cfgSvc->ctx, msg.payload);
        snprintf(ackBuf, sizeof(ackBuf), "{\"ok\":%s}", ok ? "true" : "false");
        client.publish(topicCfgAck, 1, false, ackBuf);

        if (ok) {
            // Publish only touched cfg modules for immediate state echo.
            // Fallback to full publish if patch shape cannot be resolved.
            if (!publishConfigBlocksFromPatch(msg.payload, true)) {
                publishConfigBlocks(true);
            }
        }
        return;
    }
}

void MQTTModule::init(ConfigStore& cfg, ServiceRegistry& services) {
    cfg.registerVar(hostVar);
    cfg.registerVar(portVar);
    cfg.registerVar(userVar);
    cfg.registerVar(passVar);
    cfg.registerVar(baseTopicVar);
    cfg.registerVar(enabledVar);
    cfg.registerVar(sensorMinVar);

    wifiSvc = services.get<WifiService>("wifi");
    cmdSvc = services.get<CommandService>("cmd");
    cfgSvc = services.get<ConfigStoreService>("config");
    timeSchedSvc = services.get<TimeSchedulerService>("time.scheduler");
    logHub = services.get<LogHubService>("loghub");

    auto* ebSvc = services.get<EventBusService>("eventbus");
    eventBus = ebSvc ? ebSvc->bus : nullptr;

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore = dsSvc ? dsSvc->store : nullptr;

    mqttSvc.publish = MQTTModule::svcPublish;
    mqttSvc.formatTopic = MQTTModule::svcFormatTopic;
    mqttSvc.isConnected = MQTTModule::svcIsConnected;
    mqttSvc.ctx = this;
    services.add("mqtt", &mqttSvc);

    if (eventBus) {
        eventBus->subscribe(EventId::DataChanged, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::DataSnapshotAvailable, &MQTTModule::onEventStatic, this);
        eventBus->subscribe(EventId::ConfigChanged, &MQTTModule::onEventStatic, this);
    }

    makeDeviceId(deviceId, sizeof(deviceId));
    buildTopics();

    rxQ = xQueueCreate(8, sizeof(RxMsg));

    client.onConnect([this](bool sp){ this->onConnect(sp); });
    client.onDisconnect([this](AsyncMqttClientDisconnectReason r){ this->onDisconnect(r); });
    client.onMessage([this](char* t, char* p, AsyncMqttClientMessageProperties pr, size_t l, size_t i, size_t tot){
        this->onMessage(t, p, pr, l, i, tot);
    });

    refreshConfigModules();

    LOGI("Init id=%s topic=%s cfgModules=%u", deviceId, topicCmd, (unsigned)cfgModuleCount);
    if (timeSchedSvc) {
        LOGI("Time scheduler config will be published per-slot on cfg/time/scheduler/slotN");
    }

    _netReady = dataStore ? wifiReady(*dataStore) : false;
    _netReadyTs = millis();
    _retryCount = 0;
    _retryDelayMs = 2000;

    setState(cfgData.enabled ? MQTTState::WaitingNetwork : MQTTState::Disabled);
}

void MQTTModule::loop() {
    if (!cfgData.enabled) {
        if (state != MQTTState::Disabled) {
            client.disconnect();
            setState(MQTTState::Disabled);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        return;
    }

    switch (state) {
    case MQTTState::Disabled: setState(MQTTState::WaitingNetwork); break;
    case MQTTState::WaitingNetwork:
        if (!_netReady) break;
        if (millis() - _netReadyTs >= 2000) connectMqtt();
        break;
    case MQTTState::Connecting:
        if (millis() - stateTs > 10000) {
            LOGW("Connect timeout");
            client.disconnect();
            setState(MQTTState::ErrorWait);
        }
        break;
    case MQTTState::Connected: {
        RxMsg m;
        while (xQueueReceive(rxQ, &m, 0) == pdTRUE) processRx(m);
        if (_pendingPublish) {
            _pendingPublish = false;
            publishConfigBlocks(true);
        }
        uint32_t now = millis();
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
                sensorsPendingDirtyMask &= ~DIRTY_ACTUATORS;
                if (!hasSensors) {
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

            if      (next < 5000)   next = 5000;
            else if (next < 10000)  next = 10000;
            else if (next < 30000)  next = 30000;
            else if (next < 60000)  next = 60000;
            else                    next = 300000;

            next = clampU32(next, 2000, 300000);
            _retryDelayMs = jitterMs(next, 15);
            setState(MQTTState::WaitingNetwork);
        }
        break;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
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
            client.disconnect();
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

        if (strcmp(key, "mq_base") == 0 ||
            strcmp(key, "mq_host") == 0 ||
            strcmp(key, "mq_port") == 0 ||
            strcmp(key, "mq_user") == 0 ||
            strcmp(key, "mq_pass") == 0)
        {
            LOGI("MQTT config changed (%s) -> reconnect", key);
            client.disconnect();
            _netReadyTs = millis();
            setState(MQTTState::WaitingNetwork);
        } else if (strcmp(key, "tm_sched") == 0 ||
                   strcmp(key, "tm_wkmon") == 0) {
            _pendingPublish = true;
        }
        return;
    }
}
