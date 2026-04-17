/**
 * @file AlarmModule.cpp
 * @brief Implementation file.
 */

#include "AlarmModule.h"

#include "Core/CommandRegistry.h"
#include "Core/ErrorCodes.h"
#include "Core/ModuleId.h"
#include "Core/MqttTopics.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <new>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::AlarmModule)
#include "Core/ModuleLog.h"

namespace {
static constexpr uint8_t kAlarmCfgProducerId = 46;
static constexpr uint8_t kAlarmCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kAlarmCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Alarms, kAlarmCfgBranch}, "alarms", "alarms", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
static constexpr HAButtonEntry kAlarmResetSlotButtons[] = {
    {"alarms", "alm_reset_slot_0", "Reset Alarm Slot 0", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":0}}", "diagnostic", "mdi:numeric-0-box-outline"},
    {"alarms", "alm_reset_slot_1", "Reset Alarm Slot 1", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":1}}", "diagnostic", "mdi:numeric-1-box-outline"},
    {"alarms", "alm_reset_slot_2", "Reset Alarm Slot 2", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":2}}", "diagnostic", "mdi:numeric-2-box-outline"},
    {"alarms", "alm_reset_slot_3", "Reset Alarm Slot 3", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":3}}", "diagnostic", "mdi:numeric-3-box-outline"},
    {"alarms", "alm_reset_slot_4", "Reset Alarm Slot 4", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":4}}", "diagnostic", "mdi:numeric-4-box-outline"},
    {"alarms", "alm_reset_slot_5", "Reset Alarm Slot 5", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":5}}", "diagnostic", "mdi:numeric-5-box-outline"},
    {"alarms", "alm_reset_slot_6", "Reset Alarm Slot 6", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":6}}", "diagnostic", "mdi:numeric-6-box-outline"},
    {"alarms", "alm_reset_slot_7", "Reset Alarm Slot 7", MqttTopics::SuffixCmd, "{\"cmd\":\"alarms.reset_slot\",\"args\":{\"slot\":7}}", "diagnostic", "mdi:numeric-7-box-outline"},
};
}

static uint32_t clampEvalPeriodMs_(int32_t inMs)
{
    if (inMs < 25) return 25U;
    if (inMs > 5000) return 5000U;
    return (uint32_t)inMs;
}

static bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::Alarm::JsonCmdBuf;
    static StaticJsonDocument<CMD_DOC_CAPACITY> doc;

    doc.clear();
    const char* json = req.args ? req.args : req.json;
    if (!json || json[0] == '\0') return false;

    const DeserializationError err = deserializeJson(doc, json);
    if (!err && doc.is<JsonObject>()) {
        outObj = doc.as<JsonObjectConst>();
        return true;
    }

    if (req.json && req.json[0] != '\0' && req.args != req.json) {
        doc.clear();
        const DeserializationError rootErr = deserializeJson(doc, req.json);
        if (rootErr || !doc.is<JsonObjectConst>()) return false;
        JsonVariantConst argsVar = doc["args"];
        if (argsVar.is<JsonObjectConst>()) {
            outObj = argsVar.as<JsonObjectConst>();
            return true;
        }
    }

    return false;
}

bool AlarmModule::delayReached_(uint32_t sinceMs, uint32_t delayMs, uint32_t nowMs)
{
    if (delayMs == 0U) return true;
    if (sinceMs == 0U) return false;
    return (uint32_t)(nowMs - sinceMs) >= delayMs;
}

const char* AlarmModule::condStateStr_(AlarmCondState s)
{
    if (s == AlarmCondState::True) return "true";
    if (s == AlarmCondState::False) return "false";
    return "unknown";
}

void AlarmModule::emitAlarmEvent_(EventId id, AlarmId alarmId) const
{
    if (!eventBus_) return;
    const AlarmPayload payload{(uint16_t)alarmId};
    (void)eventBus_->post(id, &payload, sizeof(payload), ModuleId::Alarm);
}

void AlarmModule::noteAlarmNotified_(AlarmId id, uint32_t nowMs)
{
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        AlarmSlot& s = slots_[(uint16_t)idx];
        s.lastNotifyMs = nowMs;
    }
    portEXIT_CRITICAL(&slotsMux_);
}

uint8_t AlarmModule::takeDueAlarmReminderIds_(AlarmId* out, uint8_t max, uint32_t nowMs)
{
    if (!out || max == 0U) return 0U;

    uint8_t count = 0U;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms && count < max; ++i) {
        AlarmSlot& s = slots_[i];
        if (!s.used || !s.active || s.lastCond != AlarmCondState::True) continue;
        const uint32_t minRepeatMs = s.def.minRepeatMs;
        if (minRepeatMs > 0U &&
            s.lastNotifyMs != 0U &&
            delayReached_(s.lastNotifyMs, minRepeatMs, nowMs)) {
            s.lastNotifyMs = nowMs;
            out[count++] = s.id;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

int16_t AlarmModule::findSlotById_(AlarmId id) const
{
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        if (!slots_[i].used) continue;
        if (slots_[i].id == id) return (int16_t)i;
    }
    return -1;
}

int16_t AlarmModule::findFreeSlot_() const
{
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        if (!slots_[i].used) return (int16_t)i;
    }
    return -1;
}

bool AlarmModule::slotAlarmId_(uint8_t slot, AlarmId& outId) const
{
    outId = AlarmId::None;
    if (slot >= Limits::Alarm::MaxAlarms) return false;

    bool ok = false;
    portENTER_CRITICAL(&slotsMux_);
    if (slots_[slot].used) {
        outId = slots_[slot].id;
        ok = true;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return ok;
}

bool AlarmModule::registerAlarm_(const AlarmRegistration& def, AlarmCondFn condFn, void* condCtx)
{
    if (!condFn) return false;
    if (def.id == AlarmId::None) return false;
    if (def.code[0] == '\0') return false;
    if (def.title[0] == '\0') return false;

    bool ok = false;
    portENTER_CRITICAL(&slotsMux_);
    if (findSlotById_(def.id) >= 0) {
        ok = false;
    } else {
        const int16_t idx = findFreeSlot_();
        if (idx >= 0) {
            AlarmSlot& s = slots_[(uint16_t)idx];
            s = AlarmSlot{};
            s.used = true;
            s.id = def.id;
            s.def = def;
            s.condFn = condFn;
            s.condCtx = condCtx;
            ok = true;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    if (ok) {
        LOGI("Alarm registered id=%u code=%s", (unsigned)def.id, def.code);
    } else {
        LOGW("Alarm registration failed id=%u", (unsigned)def.id);
    }
    return ok;
}

bool AlarmModule::reset_(AlarmId id)
{
    bool postReset = false;
    bool warnConditionTrue = false;
    bool warnNotActive = false;
    bool warnNotLatched = false;
    char alarmCode[sizeof(slots_[0].def.code)] = {0};
    AlarmCondState resetCond = AlarmCondState::Unknown;
    uint32_t nowMs = millis();

    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        AlarmSlot& s = slots_[(uint16_t)idx];
        strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
        alarmCode[sizeof(alarmCode) - 1] = '\0';
        if (s.active && s.def.latched && s.lastCond == AlarmCondState::False) {
            resetCond = s.lastCond;
            s.active = false;
            s.offSinceMs = 0U;
            s.lastChangeMs = nowMs;
            s.lastNotifyMs = nowMs;
            postReset = true;
        } else {
            resetCond = s.lastCond;
            warnConditionTrue = s.active && s.def.latched && s.lastCond == AlarmCondState::True;
            warnNotActive = !s.active;
            warnNotLatched = !s.def.latched;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    if (postReset) {
        LOGD("Alarm reset request accepted id=%u code=%s cond=%s",
             (unsigned)id,
             alarmCode[0] ? alarmCode : "?",
             condStateStr_(resetCond));
        LOGI("Alarm reset id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        emitAlarmEvent_(EventId::AlarmReset, id);
        emitAlarmEvent_(EventId::AlarmCleared, id);
    } else if (warnConditionTrue) {
        LOGW("Alarm reset denied id=%u code=%s cond=true active=1 latched=1",
             (unsigned)id,
             alarmCode[0] ? alarmCode : "?");
    } else if (warnNotActive || warnNotLatched) {
        LOGW("Alarm reset denied id=%u code=%s active=%u latched=%u cond=%s",
             (unsigned)id,
             alarmCode[0] ? alarmCode : "?",
             warnNotActive ? 0U : 1U,
             warnNotLatched ? 0U : 1U,
             condStateStr_(resetCond));
    }
    return postReset;
}

uint8_t AlarmModule::resetAll_()
{
    AlarmId pending[Limits::Alarm::MaxAlarms]{};
    uint8_t pendingCount = 0;

    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (!s.used || !s.active || !s.def.latched || s.lastCond != AlarmCondState::False) continue;
        if (pendingCount < Limits::Alarm::MaxAlarms) {
            pending[pendingCount++] = s.id;
        }
    }
    portEXIT_CRITICAL(&slotsMux_);

    uint8_t resetCount = 0;
    for (uint8_t i = 0; i < pendingCount; ++i) {
        if (reset_(pending[i])) ++resetCount;
    }
    return resetCount;
}

bool AlarmModule::isActive_(AlarmId id) const
{
    bool out = false;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) out = slots_[(uint16_t)idx].active;
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

bool AlarmModule::isResettable_(AlarmId id) const
{
    bool out = false;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        const AlarmSlot& slot = slots_[(uint16_t)idx];
        out = slot.active && slot.def.latched && slot.lastCond == AlarmCondState::False;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return out;
}

uint8_t AlarmModule::activeCount_() const
{
    uint8_t count = 0;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        if (slots_[i].used && slots_[i].active) ++count;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

AlarmSeverity AlarmModule::highestSeverity_() const
{
    AlarmSeverity highest = AlarmSeverity::Info;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = slots_[i];
        if (!s.used || !s.active) continue;
        if ((uint8_t)s.def.severity > (uint8_t)highest) highest = s.def.severity;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return highest;
}

bool AlarmModule::buildSnapshot_(char* out, size_t len) const
{
    if (!out || len == 0) return false;

    AlarmSlot snap[Limits::Alarm::MaxAlarms]{};
    portENTER_CRITICAL(&slotsMux_);
    memcpy(snap, slots_, sizeof(snap));
    portEXIT_CRITICAL(&slotsMux_);

    const uint8_t active = activeCount_();
    const AlarmSeverity highest = highestSeverity_();

    int wrote = snprintf(
        out,
        len,
        "{\"ok\":true,\"active_count\":%u,\"highest_severity\":%u,\"alarms\":[",
        (unsigned)active,
        (unsigned)((uint8_t)highest));
    if (wrote <= 0 || (size_t)wrote >= len) return false;

    size_t pos = (size_t)wrote;
    bool first = true;
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        const AlarmSlot& s = snap[i];
        if (!s.used || !s.active) continue;

        wrote = snprintf(
            out + pos,
            len - pos,
            "%s{\"id\":%u,\"code\":\"%s\",\"title\":\"%s\",\"active\":true,\"resettable\":%s,\"severity\":%u}",
            first ? "" : ",",
            (unsigned)s.id,
            s.def.code,
            s.def.title,
            (s.def.latched && s.lastCond == AlarmCondState::False) ? "true" : "false",
            (unsigned)((uint8_t)s.def.severity));
        if (wrote <= 0 || (size_t)wrote >= (len - pos)) return false;
        pos += (size_t)wrote;
        first = false;
    }

    if ((len - pos) < 3) return false;
    out[pos++] = ']';
    out[pos++] = '}';
    out[pos] = '\0';
    return true;
}

uint8_t AlarmModule::listIds_(AlarmId* out, uint8_t max) const
{
    if (!out || max == 0) return 0;
    uint8_t count = 0;
    portENTER_CRITICAL(&slotsMux_);
    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms && count < max; ++i) {
        if (!slots_[i].used) continue;
        out[count++] = slots_[i].id;
    }
    portEXIT_CRITICAL(&slotsMux_);
    return count;
}

bool AlarmModule::buildAlarmState_(AlarmId id, char* out, size_t len) const
{
    if (!out || len == 0) return false;

    AlarmSlot snap{};
    bool found = false;
    uint16_t slotIndex = 0;
    portENTER_CRITICAL(&slotsMux_);
    const int16_t idx = findSlotById_(id);
    if (idx >= 0) {
        snap = slots_[(uint16_t)idx];
        slotIndex = (uint16_t)idx;
        found = true;
    }
    portEXIT_CRITICAL(&slotsMux_);
    if (!found) return false;

    const int wrote = snprintf(
        out,
        len,
        "{\"id\":%u,\"slot\":%u,\"a\":%u,\"r\":%u,\"c\":%u,\"s\":%u,\"lc\":%lu}",
        (unsigned)snap.id,
        (unsigned)slotIndex,
        snap.active ? 1u : 0u,
        (snap.active && snap.def.latched && snap.lastCond == AlarmCondState::False) ? 1u : 0u,
        (unsigned)((uint8_t)snap.lastCond),
        (unsigned)((uint8_t)snap.def.severity),
        (unsigned long)snap.lastChangeMs);
    return (wrote > 0) && ((size_t)wrote < len);
}

bool AlarmModule::buildPacked_(char* out, size_t len, uint8_t slotCount) const
{
    if (!out || len == 0) return false;

    uint8_t n = slotCount;
    if (n == 0) n = 8;
    if (n > 8) n = 8;
    if (n > (uint8_t)Limits::Alarm::MaxAlarms) n = (uint8_t)Limits::Alarm::MaxAlarms;

    uint64_t pack = 0ULL;
    portENTER_CRITICAL(&slotsMux_);
    for (uint8_t i = 0; i < n; ++i) {
        const AlarmSlot& s = slots_[i];
        uint8_t bits = 0;
        if (s.used) {
            if (s.active) bits |= 0x01U;
            if (s.active && s.def.latched && s.lastCond == AlarmCondState::False) bits |= 0x02U;
            if (s.lastCond == AlarmCondState::True) bits |= 0x04U;
            bits |= (uint8_t)(((uint8_t)s.def.severity & 0x03U) << 3);
        }
        pack |= ((uint64_t)bits) << ((uint64_t)i * 5ULL);
    }
    portEXIT_CRITICAL(&slotsMux_);

    const int wrote = snprintf(
        out,
        len,
        "{\"v\":2,\"slots\":%u,\"p\":%llu,\"h\":\"%010llX\",\"ts\":%lu}",
        (unsigned)n,
        (unsigned long long)pack,
        (unsigned long long)pack,
        (unsigned long)millis());
    return (wrote > 0) && ((size_t)wrote < len);
}

uint32_t AlarmModule::buildRuntimeMask_(RuntimeUiValueId valueId) const
{
    uint32_t mask = 0U;
    portENTER_CRITICAL(&slotsMux_);
    for (uint8_t i = 0; i < (uint8_t)Limits::Alarm::MaxAlarms && i < 32U; ++i) {
        const AlarmSlot& slot = slots_[i];
        if (!slot.used) continue;

        bool setBit = false;
        if (valueId == RuntimeUiActiveMask) {
            setBit = slot.active;
        } else if (valueId == RuntimeUiResettableMask) {
            setBit = slot.active && slot.def.latched && slot.lastCond == AlarmCondState::False;
        } else if (valueId == RuntimeUiConditionMask) {
            setBit = slot.lastCond == AlarmCondState::True;
        }

        if (setBit) mask |= (1UL << i);
    }
    portEXIT_CRITICAL(&slotsMux_);
    return mask;
}

bool AlarmModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    if (valueId == RuntimeUiActiveMask ||
        valueId == RuntimeUiResettableMask ||
        valueId == RuntimeUiConditionMask) {
        return writer.writeU32(runtimeId, buildRuntimeMask_((RuntimeUiValueId)valueId));
    }
    return false;
}

bool AlarmModule::registerAlarmSvc_(const AlarmRegistration* def, AlarmCondFn condFn, void* condCtx)
{
    if (!def) return false;
    return registerAlarm_(*def, condFn, condCtx);
}

bool AlarmModule::cmdList_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    if (!self->buildSnapshot_(reply, replyLen)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InternalAckOverflow, "alarms.list")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    return true;
}

bool AlarmModule::handleCmdReset_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingArgs, "alarms.reset")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args.containsKey("id")) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingValue, "alarms.reset.id")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args["id"].is<uint16_t>() && !args["id"].is<uint32_t>() && !args["id"].is<int32_t>()) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InvalidEventId, "alarms.reset.id")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    const uint32_t idRaw = args["id"].as<uint32_t>();
    const AlarmId id = (AlarmId)((uint16_t)idRaw);
    if (!reset_(id)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "alarms.reset")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"id\":%u}", (unsigned)((uint16_t)id));
    return true;
}

bool AlarmModule::handleCmdResetSlot_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingArgs, "alarms.reset_slot")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args.containsKey("slot")) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::MissingSlot, "alarms.reset_slot.slot")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    if (!args["slot"].is<uint8_t>() && !args["slot"].is<uint16_t>() &&
        !args["slot"].is<uint32_t>() && !args["slot"].is<int32_t>()) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InvalidSlot, "alarms.reset_slot.slot")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    const uint32_t slotRaw = args["slot"].as<uint32_t>();
    if (slotRaw >= 8U || slotRaw >= (uint32_t)Limits::Alarm::MaxAlarms) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::InvalidSlot, "alarms.reset_slot.slot")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }
    const uint8_t slot = (uint8_t)slotRaw;

    AlarmId id = AlarmId::None;
    if (!slotAlarmId_(slot, id)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::UnusedSlot, "alarms.reset_slot.slot")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    if (!reset_(id)) {
        if (!writeErrorJson(reply, replyLen, ErrorCode::Failed, "alarms.reset_slot")) {
            snprintf(reply, replyLen, "{\"ok\":false}");
        }
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"id\":%u}",
             (unsigned)slot, (unsigned)((uint16_t)id));
    return true;
}

bool AlarmModule::cmdReset_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    return self->handleCmdReset_(req, reply, replyLen);
}

bool AlarmModule::cmdResetSlot_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    return self->handleCmdResetSlot_(req, reply, replyLen);
}

bool AlarmModule::cmdResetAll_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    AlarmModule* self = static_cast<AlarmModule*>(userCtx);
    if (!self) return false;
    const uint8_t resetCount = self->resetAll_();
    snprintf(reply, replyLen, "{\"ok\":true,\"reset\":%u}", (unsigned)resetCount);
    return true;
}

void AlarmModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Alarms;
    constexpr uint8_t kCfgBranchId = kAlarmCfgBranch;
    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(evalPeriodVar_, kCfgModuleId, kCfgBranchId);

    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    const EventBusService* eb = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = eb ? eb->bus : nullptr;
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    haSvc_ = services.get<HAService>(ServiceId::Ha);

    if (!services.add(ServiceId::Alarm, &alarmSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Alarm));
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.list", &AlarmModule::cmdList_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.reset", &AlarmModule::cmdReset_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.reset_slot", &AlarmModule::cmdResetSlot_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "alarms.reset_all", &AlarmModule::cmdResetAll_, this);
    }

    LOGI("Alarm service registered");
    (void)logHub_;
}

void AlarmModule::registerHaEntities_(ServiceRegistry& services)
{
    if (haEntitiesRegistered_) return;
    if (!haSvc_) haSvc_ = services.get<HAService>(ServiceId::Ha);
    if (!haSvc_) return;

    bool registeredAny = false;

    if (haSvc_->addSensor) {
        const HASensorEntry alarmsPack{
            "alarms",
            "alm_pack",
            "Alarms Pack",
            "rt/alarms/p",
            "{{ value_json.p | int(0) }}",
            "diagnostic",
            "mdi:alarm-light-outline",
            nullptr,
            false,
            nullptr
        };
        if (haSvc_->addSensor(haSvc_->ctx, &alarmsPack)) {
            registeredAny = true;
        } else {
            LOGW("HA registration failed: alm_pack");
        }
    }

    if (haSvc_->addBinarySensor) {
        const HABinarySensorEntry waterLevelLow{
            "alarms",
            "alm_wlvl_low",
            "Pool Water Level Low",
            "rt/alarms/id1006",
            "{{ 'True' if value_json.a | int(0) else 'False' }}",
            "problem",
            nullptr,
            "mdi:waves-arrow-down"
        };
        if (haSvc_->addBinarySensor(haSvc_->ctx, &waterLevelLow)) {
            registeredAny = true;
        } else {
            LOGW("HA registration failed: alm_wlvl_low");
        }
    }

    if (haSvc_->addButton) {
        const HAButtonEntry resetAll{
            "alarms",
            "alm_reset_all",
            "Reset Cleared Latched Alarms",
            MqttTopics::SuffixCmd,
            "{\\\"cmd\\\":\\\"alarms.reset_all\\\"}",
            "diagnostic",
            "mdi:alarm-off"
        };
        if (haSvc_->addButton(haSvc_->ctx, &resetAll)) {
            registeredAny = true;
        } else {
            LOGW("HA registration failed: alm_reset_all");
        }

        for (uint8_t i = 0; i < (uint8_t)(sizeof(kAlarmResetSlotButtons) / sizeof(kAlarmResetSlotButtons[0])); ++i) {
            if (haSvc_->addButton(haSvc_->ctx, &kAlarmResetSlotButtons[i])) {
                registeredAny = true;
            } else {
                LOGW("HA registration failed: %s", kAlarmResetSlotButtons[i].objectSuffix);
            }
        }
    }

    haEntitiesRegistered_ = registeredAny;
}

void AlarmModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kAlarmCfgProducerId,
                               kAlarmCfgRoutes,
                               (uint8_t)(sizeof(kAlarmCfgRoutes) / sizeof(kAlarmCfgRoutes[0])),
                               services);
    }
    evalPeriodMsCfg_ = (int32_t)clampEvalPeriodMs_(evalPeriodMsCfg_);
    registerHaEntities_(services);
}

void AlarmModule::evaluateOnce_(uint32_t nowMs)
{
    AlarmId dueNotifyIds[Limits::Alarm::MaxAlarms]{};
    const uint8_t dueNotifyCount = takeDueAlarmReminderIds_(dueNotifyIds, (uint8_t)Limits::Alarm::MaxAlarms, nowMs);
    for (uint8_t i = 0; i < dueNotifyCount; ++i) {
        emitAlarmEvent_(EventId::AlarmConditionChanged, dueNotifyIds[i]);
    }

    for (uint16_t i = 0; i < Limits::Alarm::MaxAlarms; ++i) {
        AlarmId id = AlarmId::None;
        AlarmCondFn condFn = nullptr;
        void* condCtx = nullptr;
        bool used = false;

        portENTER_CRITICAL(&slotsMux_);
        if (slots_[i].used) {
            used = true;
            id = slots_[i].id;
            condFn = slots_[i].condFn;
            condCtx = slots_[i].condCtx;
        }
        portEXIT_CRITICAL(&slotsMux_);

        if (!used || !condFn) continue;

        const AlarmCondState cond = condFn(condCtx, nowMs);
        bool postRaised = false;
        bool postCleared = false;
        bool postCondTrue = false;
        bool postCondFalse = false;
        char alarmCode[sizeof(slots_[0].def.code)] = {0};

        portENTER_CRITICAL(&slotsMux_);
        AlarmSlot& s = slots_[i];
        if (s.used && s.id == id && s.condFn == condFn && s.condCtx == condCtx) {
            const AlarmCondState prevCond = s.lastCond;
            if (prevCond != cond) {
                if (cond == AlarmCondState::True) {
                    postCondTrue = true;
                    strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                    alarmCode[sizeof(alarmCode) - 1] = '\0';
                } else if (cond == AlarmCondState::False) {
                    postCondFalse = true;
                    strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                    alarmCode[sizeof(alarmCode) - 1] = '\0';
                }
            }
            s.lastCond = cond;

            if (cond == AlarmCondState::True) {
                s.offSinceMs = 0U;
                if (!s.active) {
                    if (s.onSinceMs == 0U) s.onSinceMs = nowMs;
                    if (delayReached_(s.onSinceMs, s.def.onDelayMs, nowMs)) {
                        s.active = true;
                        s.activeSinceMs = nowMs;
                        s.lastChangeMs = nowMs;
                        s.onSinceMs = 0U;
                        postRaised = true;
                        strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                        alarmCode[sizeof(alarmCode) - 1] = '\0';
                    }
                } else {
                    s.onSinceMs = 0U;
                }
            } else if (cond == AlarmCondState::False) {
                s.onSinceMs = 0U;
                if (s.active) {
                    if (!s.def.latched) {
                        if (s.offSinceMs == 0U) s.offSinceMs = nowMs;
                        if (delayReached_(s.offSinceMs, s.def.offDelayMs, nowMs)) {
                            s.active = false;
                            s.offSinceMs = 0U;
                            s.lastChangeMs = nowMs;
                            postCleared = true;
                            strncpy(alarmCode, s.def.code, sizeof(alarmCode) - 1);
                            alarmCode[sizeof(alarmCode) - 1] = '\0';
                        }
                    } else {
                        s.offSinceMs = 0U;
                    }
                } else {
                    s.offSinceMs = 0U;
                }
            } else {
                // Unknown sensor/state: cancel transition timers, keep stable alarm state.
                s.onSinceMs = 0U;
                s.offSinceMs = 0U;
            }
        }
        portEXIT_CRITICAL(&slotsMux_);

        if (postCondTrue) {
            LOGD("Alarm cond=true id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        }
        if (postCondFalse) {
            LOGD("Alarm cond=false id=%u code=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?");
        }
        if (postRaised) {
            LOGI("Alarm raised id=%u code=%s cond=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?", condStateStr_(cond));
        }
        if (postCleared) {
            LOGI("Alarm cleared id=%u code=%s cond=%s", (unsigned)id, alarmCode[0] ? alarmCode : "?", condStateStr_(cond));
        }

        if (postRaised) {
            noteAlarmNotified_(id, nowMs);
            emitAlarmEvent_(EventId::AlarmRaised, id);
        } else if (postCleared) {
            noteAlarmNotified_(id, nowMs);
            emitAlarmEvent_(EventId::AlarmCleared, id);
        } else if (postCondTrue || postCondFalse) {
            noteAlarmNotified_(id, nowMs);
            emitAlarmEvent_(EventId::AlarmConditionChanged, id);
        }
    }
}

void AlarmModule::loop()
{
    if (!enabled_) {
        vTaskDelay(pdMS_TO_TICKS(500));
        return;
    }

    evaluateOnce_(millis());
    vTaskDelay(pdMS_TO_TICKS(clampEvalPeriodMs_(evalPeriodMsCfg_)));
}
