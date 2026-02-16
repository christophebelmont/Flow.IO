/**
 * @file MQTTModule.cpp
 * @brief Implementation file.
 */
#include "MQTTModule.h"
#include "Core/Runtime.h"
#include "Core/MqttTopics.h"
#include "Core/SystemLimits.h"
#include "Core/SystemStats.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
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

static bool isTimeSchedKey(const char* key)
{
    return isAnyOf(key, {
        NvsKeys::Time::ScheduleBlob,
        NvsKeys::Time::WeekStartMonday
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
}

void MQTTModule::onDisconnect(AsyncMqttClientDisconnectReason) {
    LOGW("Disconnected");
    setState(MQTTState::ErrorWait);
}

void MQTTModule::onMessage(char* topic, char* payload, AsyncMqttClientMessageProperties,
                           size_t len, size_t, size_t total) {
    if (!rxQ) return;
    if (!topic || !payload) {
        countRxDrop_();
        return;
    }
    if (len != total) {
        countRxDrop_();
        return;
    }

    const size_t topicCap = sizeof(RxMsg{}.topic);
    const size_t payloadCap = sizeof(RxMsg{}.payload);
    size_t topicLen = topic ? strlen(topic) : 0U;
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
        if (!writeErrorJson(stateCfgBuf, sizeof(stateCfgBuf), ErrorCode::CfgTruncated, "cfg")) {
            snprintf(stateCfgBuf, sizeof(stateCfgBuf), "{\"ok\":false}");
        }
        if (!publish(topicCfgBlocks[idx], stateCfgBuf, 1, retained)) {
            LOGW("cfg/%s publish failed (truncated payload)", cfgModules[idx]);
            return false;
        }
        return true;
    }
    if (!any) return false;
    if (!publish(topicCfgBlocks[idx], stateCfgBuf, 1, retained)) {
        LOGW("cfg/%s publish failed", cfgModules[idx]);
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
    const int tw = snprintf(moduleTopic, sizeof(moduleTopic), "%s/%s/cfg/%s", cfgData.baseTopic, deviceId, module);
    if (!(tw > 0 && (size_t)tw < sizeof(moduleTopic))) {
        LOGW("cfg publish: topic truncated for module=%s", module);
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
            LOGW("cfg/%s publish failed (truncated payload)", module);
            return false;
        }
        return true;
    }
    if (!any) return false;

    if (!publish(moduleTopic, stateCfgBuf, 1, retained)) {
        LOGW("cfg/%s publish failed", module);
        return false;
    }
    return true;
}

void MQTTModule::publishTimeSchedulerSlots(bool retained, const char* rootTopic)
{
    if (!rootTopic || rootTopic[0] == '\0') return;

    // Root topic stays small and indicates that details are split by slot.
    if (!publish(rootTopic, "{\"mode\":\"per_slot\",\"slots\":16}", 1, retained)) {
        LOGW("cfg/time/scheduler root publish failed");
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
                LOGW("cfg/time/scheduler slot%u publish failed (unused)", (unsigned)slot);
            }
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
        if (!publish(slotTopic, stateCfgBuf, 1, retained)) {
            LOGW("cfg/time/scheduler slot%u publish failed", (unsigned)slot);
        }
    }
}

bool MQTTModule::publish(const char* topic, const char* payload, int qos, bool retain)
{
    if (!topic || !payload) return false;
    if (state != MQTTState::Connected) return false;
    const uint16_t packetId = client.publish(topic, qos, retain, payload);
    if (packetId == 0U) {
        LOGW("mqtt publish rejected topic=%s qos=%d retain=%d", topic, qos, retain ? 1 : 0);
        return false;
    }
    LOGI("MQTT TX t=%s r=%d %d %s", topic, retain ? 1 : 0, (unsigned)packetId, payload);
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

    _suppressConfigChangedUntilMs = millis() + Limits::Mqtt::Timing::CfgEchoSuppressMs;
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

    // Publish config echo only for the command family module, to avoid flooding cfg/*
    // and delaying subsequent rx handling on congested links.
    char module[Limits::Mqtt::Buffers::CmdModule] = {0};
    static constexpr const char* kTimeSchedulerCmdPrefix = "time.scheduler.";
    if (strncmp(cmd, kTimeSchedulerCmdPrefix, sizeof("time.scheduler.") - 1U) == 0) {
        strncpy(module, "time/scheduler", sizeof(module) - 1);
    } else {
        const char* dot = strchr(cmd, '.');
        if (dot) {
            const size_t moduleLen = (size_t)(dot - cmd);
            if (moduleLen > 0 && moduleLen < sizeof(module)) {
                memcpy(module, cmd, moduleLen);
                module[moduleLen] = '\0';
            }
        }
    }
    if (module[0] != '\0') {
        if (strcmp(module, "pool") == 0) {
            strncpy(module, "pooldevice", sizeof(module) - 1);
            module[sizeof(module) - 1] = '\0';
        }
        (void)publishConfigModuleByName_(module, true);
    }
    _pendingPublish = false;
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

    _suppressConfigChangedUntilMs = millis() + Limits::Mqtt::Timing::CfgEchoSuppressMs;
    bool ok = cfgSvc->applyJson(cfgSvc->ctx, msg.payload);
    if (!ok) {
        publishRxError_(topicCfgAck, ErrorCode::CfgApplyFailed, "cfg/set", false);
        return;
    }

    // Publish touched cfg modules immediately from the received patch.
    // This avoids any deferred path and keeps cfg/* state in sync right after cfg/set.
    bool publishedAny = false;
    JsonObjectConst patch = cfgDoc.as<JsonObjectConst>();
    for (JsonPairConst kv : patch) {
        const char* module = kv.key().c_str();
        if (!module || module[0] == '\0') continue;
        publishedAny = publishConfigModuleByName_(module, true) || publishedAny;
    }

    if (!publishedAny) {
        // Conservative fallback: publish full cfg tree when no touched module was emitted.
        publishConfigBlocks(true);
    } else {
        // cfg/set already emitted touched modules immediately; avoid redundant full-tree burst.
        _pendingPublish = false;
    }

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
    rxDropCount_ = 0;
    oversizeDropCount_ = 0;
    parseFailCount_ = 0;
    handlerFailCount_ = 0;
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
    }

    makeDeviceId(deviceId, sizeof(deviceId));
    buildTopics();

    rxQ = xQueueCreate(Limits::Mqtt::Capacity::RxQueueLen, sizeof(RxMsg));

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
    _retryDelayMs = Limits::Mqtt::Backoff::MinMs;

    setState(cfgData.enabled ? MQTTState::WaitingNetwork : MQTTState::Disabled);
}

void MQTTModule::loop() {
    if (!cfgData.enabled) {
        if (state != MQTTState::Disabled) {
            client.disconnect();
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
            client.disconnect();
            setState(MQTTState::ErrorWait);
        }
        break;
    case MQTTState::Connected: {
        RxMsg m;
        while (xQueueReceive(rxQ, &m, 0) == pdTRUE) processRx(m);
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
            publishConfigBlocks(true);
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

        if (isMqttConnKey(key)) {
            LOGI("MQTT config changed (%s) -> reconnect", key);
            client.disconnect();
            _netReadyTs = millis();
            setState(MQTTState::WaitingNetwork);
        } else {
            const uint32_t now = millis();
            if ((int32_t)(now - _suppressConfigChangedUntilMs) < 0) {
                // cfg/set and command paths already publish targeted cfg modules immediately.
                return;
            }

            if (isTimeSchedKey(key)) {
                _pendingPublish = true;
            } else {
                // Keep cfg/* topics in sync when config changes outside MQTT cfg/cmd handlers.
                _pendingPublish = true;
            }
        }
        return;
    }
}
