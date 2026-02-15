/**
 * @file PoolDeviceModule.cpp
 * @brief Implementation file.
 */

#include "PoolDeviceModule.h"
#include "Core/ErrorCodes.h"
#include "Core/NvsKeys.h"
#include "Core/SystemLimits.h"
#define LOG_TAG "PoolDevc"
#include "Core/ModuleLog.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool parseCmdArgsObject_(const CommandRequest& req, JsonObjectConst& outObj)
{
    static constexpr size_t CMD_DOC_CAPACITY = Limits::JsonCmdPoolDeviceBuf;
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

static void writeCmdError_(char* reply, size_t replyLen, const char* where, ErrorCode code)
{
    if (!writeErrorJson(reply, replyLen, code, where)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}

static void writeCmdErrorSlot_(char* reply, size_t replyLen, const char* where, ErrorCode code, uint8_t slot)
{
    if (!writeErrorJsonWithSlot(reply, replyLen, code, where, slot)) {
        snprintf(reply, replyLen, "{\"ok\":false}");
    }
}

bool PoolDeviceModule::defineDevice(const PoolDeviceDefinition& def)
{
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) continue;

        PoolDeviceSlot& s = slots_[i];
        s.used = true;
        s.def = def;
        snprintf(s.id, sizeof(s.id), "pd%u", (unsigned)i);

        if (s.def.label[0] == '\0') {
            strncpy(s.def.label, s.id, sizeof(s.def.label) - 1);
            s.def.label[sizeof(s.def.label) - 1] = '\0';
        }
        if (s.def.ioId == IO_ID_INVALID) {
            LOGW("Pool device %s missing ioId", s.id);
            s.used = false;
            return false;
        }
        s.ioId = s.def.ioId;
        s.desiredOn = false;
        s.actualOn = false;
        s.blockReason = POOL_DEVICE_BLOCK_NONE;

        if (s.def.tankCapacityMl > 0.0f) {
            float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
            if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
            if (initial < 0.0f) initial = 0.0f;
            s.tankRemainingMl = initial;
        } else {
            s.tankRemainingMl = 0.0f;
        }

        return true;
    }
    return false;
}

const char* PoolDeviceModule::deviceLabel(uint8_t idx) const
{
    if (idx >= POOL_DEVICE_MAX) return nullptr;
    const PoolDeviceSlot& s = slots_[idx];
    if (!s.used) return nullptr;
    return (s.def.label[0] != '\0') ? s.def.label : s.id;
}

const char* PoolDeviceModule::typeStr_(uint8_t type)
{
    if (type == POOL_DEVICE_RT_FILTRATION) return "filtration";
    if (type == POOL_DEVICE_RT_PERISTALTIC) return "peristaltic";
    return "relay";
}

const char* PoolDeviceModule::blockReasonStr_(uint8_t reason)
{
    if (reason == POOL_DEVICE_BLOCK_DISABLED) return "disabled";
    if (reason == POOL_DEVICE_BLOCK_INTERLOCK) return "interlock";
    if (reason == POOL_DEVICE_BLOCK_IO_ERROR) return "io_error";
    return "none";
}

uint8_t PoolDeviceModule::runtimeSnapshotCount() const
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    return count;
}

bool PoolDeviceModule::snapshotSlotFromIndex_(uint8_t snapshotIdx, uint8_t& slotIdxOut) const
{
    uint8_t seen = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (!slots_[i].used) continue;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            return true;
        }
        ++seen;
    }
    return false;
}

bool PoolDeviceModule::buildDeviceSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slots_[slotIdx].used) return false;

    PoolDeviceRuntimeEntry entry{};
    if (!poolDeviceRuntime(*dataStore_, slotIdx, entry)) return false;

    const char* label = deviceLabel(slotIdx);
    const char* typeStr = typeStr_(entry.type);
    const char* blockReason = blockReasonStr_(entry.blockReason);
    int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\",\"enabled\":%s,\"desired\":%s,\"on\":%s,"
        "\"type\":\"%s\",\"block\":\"%s\","
        "\"running\":{\"day_s\":%lu,\"week_s\":%lu,\"month_s\":%lu,\"total_s\":%lu},"
        "\"injected\":{\"day_ml\":%.3f,\"week_ml\":%.3f,\"month_ml\":%.3f,\"total_ml\":%.3f},"
        "\"tank\":{\"remaining_ml\":%.3f},\"ts\":%lu}",
        (unsigned)slotIdx,
        (label && label[0] != '\0') ? label : "pd",
        entry.enabled ? "true" : "false",
        entry.desiredOn ? "true" : "false",
        entry.actualOn ? "true" : "false",
        typeStr,
        blockReason,
        (unsigned long)entry.runningSecDay,
        (unsigned long)entry.runningSecWeek,
        (unsigned long)entry.runningSecMonth,
        (unsigned long)entry.runningSecTotal,
        (double)entry.injectedMlDay,
        (double)entry.injectedMlWeek,
        (double)entry.injectedMlMonth,
        (double)entry.injectedMlTotal,
        (double)entry.tankRemainingMl,
        (unsigned long)millis()
    );
    if (wrote < 0 || (size_t)wrote >= len) return false;

    // Ensure one initial publish even if runtime timestamp has not been set yet.
    maxTsOut = (entry.timestampMs == 0U) ? 1U : entry.timestampMs;
    return true;
}

const char* PoolDeviceModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    if (!snapshotSlotFromIndex_(idx, slotIdx)) return nullptr;

    static char suffix[24];
    snprintf(suffix, sizeof(suffix), "rt/pdm/pd%u", (unsigned)slotIdx);
    return suffix;
}

bool PoolDeviceModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    uint8_t slotIdx = 0xFF;
    if (!snapshotSlotFromIndex_(idx, slotIdx)) return false;
    return buildDeviceSnapshot_(slotIdx, out, len, maxTsOut);
}

uint8_t PoolDeviceModule::svcCount_(void* ctx)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    return self ? self->activeCount_() : 0;
}

PoolDeviceSvcStatus PoolDeviceModule::svcMeta_(void* ctx, uint8_t slot, PoolDeviceSvcMeta* outMeta)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    if (!self) return POOLDEV_SVC_ERR_INVALID_ARG;
    return self->svcMetaImpl_(slot, outMeta);
}

PoolDeviceSvcStatus PoolDeviceModule::svcReadActualOn_(void* ctx, uint8_t slot, uint8_t* outOn, uint32_t* outTsMs)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    if (!self) return POOLDEV_SVC_ERR_INVALID_ARG;
    return self->svcReadActualOnImpl_(slot, outOn, outTsMs);
}

PoolDeviceSvcStatus PoolDeviceModule::svcWriteDesired_(void* ctx, uint8_t slot, uint8_t on)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    if (!self) return POOLDEV_SVC_ERR_INVALID_ARG;
    return self->svcWriteDesiredImpl_(slot, on);
}

PoolDeviceSvcStatus PoolDeviceModule::svcRefillTank_(void* ctx, uint8_t slot, float remainingMl)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(ctx);
    if (!self) return POOLDEV_SVC_ERR_INVALID_ARG;
    return self->svcRefillTankImpl_(slot, remainingMl);
}

uint8_t PoolDeviceModule::activeCount_() const
{
    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    return count;
}

PoolDeviceSvcStatus PoolDeviceModule::svcMetaImpl_(uint8_t slot, PoolDeviceSvcMeta* outMeta) const
{
    if (!outMeta) return POOLDEV_SVC_ERR_INVALID_ARG;
    if (slot >= POOL_DEVICE_MAX) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    const PoolDeviceSlot& s = slots_[slot];
    if (!s.used) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;

    *outMeta = PoolDeviceSvcMeta{};
    outMeta->slot = slot;
    outMeta->used = 1;
    outMeta->type = s.def.type;
    outMeta->enabled = s.def.enabled ? 1U : 0U;
    outMeta->blockReason = s.blockReason;
    outMeta->ioId = s.ioId;
    strncpy(outMeta->runtimeId, s.id, sizeof(outMeta->runtimeId) - 1);
    outMeta->runtimeId[sizeof(outMeta->runtimeId) - 1] = '\0';
    strncpy(outMeta->label, s.def.label, sizeof(outMeta->label) - 1);
    outMeta->label[sizeof(outMeta->label) - 1] = '\0';
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcReadActualOnImpl_(uint8_t slot, uint8_t* outOn, uint32_t* outTsMs) const
{
    if (!outOn) return POOLDEV_SVC_ERR_INVALID_ARG;
    if (slot >= POOL_DEVICE_MAX) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    const PoolDeviceSlot& s = slots_[slot];
    if (!s.used) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    if (!runtimeReady_) return POOLDEV_SVC_ERR_NOT_READY;

    *outOn = s.actualOn ? 1U : 0U;
    if (outTsMs) *outTsMs = s.runtimeTsMs;
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcWriteDesiredImpl_(uint8_t slot, uint8_t on)
{
    if (slot >= POOL_DEVICE_MAX) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    PoolDeviceSlot& s = slots_[slot];
    if (!s.used) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    if (!runtimeReady_) return POOLDEV_SVC_ERR_NOT_READY;

    const bool requested = (on != 0U);
    if (requested) {
        if (!s.def.enabled) {
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            return POOLDEV_SVC_ERR_DISABLED;
        }
        if (!dependenciesSatisfied_(slot)) {
            s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            return POOLDEV_SVC_ERR_INTERLOCK;
        }
    }

    s.desiredOn = requested;
    if (!requested) s.blockReason = POOL_DEVICE_BLOCK_NONE;

    if (s.actualOn != requested) {
        if (writeIo_(s.ioId, requested)) {
            s.actualOn = requested;
            s.blockReason = POOL_DEVICE_BLOCK_NONE;
        } else {
            s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            tickDevices_(millis());
            return POOLDEV_SVC_ERR_IO;
        }
    }

    tickDevices_(millis());
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcRefillTankImpl_(uint8_t slot, float remainingMl)
{
    if (slot >= POOL_DEVICE_MAX) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    PoolDeviceSlot& s = slots_[slot];
    if (!s.used) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;

    float remaining = remainingMl;
    if (remaining < 0.0f) remaining = 0.0f;
    if (s.def.tankCapacityMl > 0.0f && remaining > s.def.tankCapacityMl) {
        remaining = s.def.tankCapacityMl;
    }

    s.tankRemainingMl = remaining;
    if (runtimeReady_) tickDevices_(millis());
    return POOLDEV_SVC_OK;
}

bool PoolDeviceModule::cmdPoolWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolWrite_(req, reply, replyLen);
}

bool PoolDeviceModule::cmdPoolRefill_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    PoolDeviceModule* self = static_cast<PoolDeviceModule*>(userCtx);
    if (!self) return false;
    return self->handlePoolRefill_(req, reply, replyLen);
}

bool PoolDeviceModule::handlePoolWrite_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pool.write", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pool.write", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pool.write", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pool.write", ErrorCode::BadSlot);
        return false;
    }

    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "pool.write", ErrorCode::MissingValue);
        return false;
    }

    JsonVariantConst value = args["value"];
    bool requested = false;
    if (value.is<bool>()) {
        requested = value.as<bool>();
    } else if (value.is<int32_t>() || value.is<uint32_t>() || value.is<float>()) {
        requested = (value.as<float>() != 0.0f);
    } else if (value.is<const char*>()) {
        const char* s = value.as<const char*>();
        if (!s) s = "0";
        if (strcmp(s, "true") == 0) requested = true;
        else if (strcmp(s, "false") == 0) requested = false;
        else requested = (atoi(s) != 0);
    } else {
        writeCmdError_(reply, replyLen, "pool.write", ErrorCode::MissingValue);
        return false;
    }

    const PoolDeviceSvcStatus st = svcWriteDesiredImpl_(slot, requested ? 1U : 0U);
    if (st != POOLDEV_SVC_OK) {
        ErrorCode code = ErrorCode::Failed;
        if (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) code = ErrorCode::UnknownSlot;
        else if (st == POOLDEV_SVC_ERR_NOT_READY) code = ErrorCode::NotReady;
        else if (st == POOLDEV_SVC_ERR_DISABLED) code = ErrorCode::Disabled;
        else if (st == POOLDEV_SVC_ERR_INTERLOCK) code = ErrorCode::InterlockBlocked;
        else if (st == POOLDEV_SVC_ERR_IO) code = ErrorCode::IoError;
        writeCmdErrorSlot_(reply, replyLen, "pool.write", code, slot);
        return false;
    }

    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u}", (unsigned)slot);
    return true;
}

bool PoolDeviceModule::handlePoolRefill_(const CommandRequest& req, char* reply, size_t replyLen)
{
    JsonObjectConst args;
    if (!parseCmdArgsObject_(req, args)) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pool.refill", ErrorCode::BadSlot);
        return false;
    }

    float remaining = slots_[slot].def.tankCapacityMl;
    if (args.containsKey("remaining_ml")) {
        JsonVariantConst rem = args["remaining_ml"];
        if (rem.is<float>() || rem.is<double>() || rem.is<int32_t>() || rem.is<uint32_t>()) {
            remaining = rem.as<float>();
        } else if (rem.is<const char*>()) {
            const char* s = rem.as<const char*>();
            remaining = s ? (float)atof(s) : remaining;
        }
    }

    const PoolDeviceSvcStatus st = svcRefillTankImpl_(slot, remaining);
    if (st != POOLDEV_SVC_OK) {
        const ErrorCode code = (st == POOLDEV_SVC_ERR_UNKNOWN_SLOT) ? ErrorCode::UnknownSlot : ErrorCode::Failed;
        writeCmdErrorSlot_(reply, replyLen, "pool.refill", code, slot);
        return false;
    }

    const float applied = slots_[slot].tankRemainingMl;
    snprintf(reply, replyLen, "{\"ok\":true,\"slot\":%u,\"remaining_ml\":%.1f}", (unsigned)slot, (double)applied);
    return true;
}

bool PoolDeviceModule::configureRuntime_()
{
    if (runtimeReady_) return true;
    if (!ioSvc_) return false;

    const uint32_t now = millis();
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        IoEndpointMeta meta{};
        if (ioSvc_->meta(ioSvc_->ctx, s.ioId, &meta) != IO_OK) {
            LOGW("Pool device %s invalid ioId=%u", s.id, (unsigned)s.ioId);
            return false;
        }
        if (meta.kind != IO_KIND_DIGITAL_OUT || (meta.capabilities & IO_CAP_W) == 0) {
            LOGW("Pool device %s ioId=%u is not writable digital output", s.id, (unsigned)s.ioId);
            return false;
        }

        if (s.def.tankCapacityMl > 0.0f) {
            float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
            if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
            if (initial < 0.0f) initial = 0.0f;
            s.tankRemainingMl = initial;
        } else {
            s.tankRemainingMl = 0.0f;
        }

        s.lastTickMs = now;
        s.runtimeTsMs = now;
        s.lastRuntimeCommitMs = now;

        PoolDeviceRuntimeEntry rt{};
        rt.valid = true;
        rt.enabled = s.def.enabled;
        rt.desiredOn = s.desiredOn;
        rt.actualOn = s.actualOn;
        rt.type = s.def.type;
        rt.blockReason = s.blockReason;
        rt.tankRemainingMl = s.tankRemainingMl;
        rt.timestampMs = s.runtimeTsMs;
        if (dataStore_) {
            (void)setPoolDeviceRuntime(*dataStore_, i, rt, DIRTY_SENSORS);
        }
    }

    runtimeReady_ = true;
    return true;
}

void PoolDeviceModule::onEventStatic_(const Event& e, void* user)
{
    if (!user) return;
    static_cast<PoolDeviceModule*>(user)->onEvent_(e);
}

void PoolDeviceModule::onEvent_(const Event& e)
{
    if (e.id != EventId::SchedulerEventTriggered) return;
    if (!e.payload || e.len < sizeof(SchedulerEventTriggeredPayload)) return;

    const SchedulerEventTriggeredPayload* p = (const SchedulerEventTriggeredPayload*)e.payload;
    if ((SchedulerEdge)p->edge != SchedulerEdge::Trigger) return;

    uint8_t pending = 0;
    if (p->eventId == TIME_EVENT_SYS_DAY_START) {
        pending = RESET_PENDING_DAY;
    } else if (p->eventId == TIME_EVENT_SYS_WEEK_START) {
        pending = RESET_PENDING_WEEK;
    } else if (p->eventId == TIME_EVENT_SYS_MONTH_START) {
        pending = RESET_PENDING_MONTH;
    } else {
        return;
    }

    portENTER_CRITICAL(&resetMux_);
    resetPendingMask_ |= pending;
    portEXIT_CRITICAL(&resetMux_);
}

void PoolDeviceModule::resetDailyCounters_()
{
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsDay = 0;
        s.injectedMlDay = 0.0f;
    }
}

void PoolDeviceModule::resetWeeklyCounters_()
{
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsWeek = 0;
        s.injectedMlWeek = 0.0f;
    }
}

void PoolDeviceModule::resetMonthlyCounters_()
{
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsMonth = 0;
        s.injectedMlMonth = 0.0f;
    }
}

void PoolDeviceModule::tickDevices_(uint32_t nowMs)
{
    uint8_t pending = 0;
    portENTER_CRITICAL(&resetMux_);
    pending = resetPendingMask_;
    resetPendingMask_ = 0;
    portEXIT_CRITICAL(&resetMux_);

    if (pending & RESET_PENDING_DAY) resetDailyCounters_();
    if (pending & RESET_PENDING_WEEK) resetWeeklyCounters_();
    if (pending & RESET_PENDING_MONTH) resetMonthlyCounters_();

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        bool changed = false;

        if (s.lastTickMs == 0) s.lastTickMs = nowMs;
        const uint32_t deltaMs = (uint32_t)(nowMs - s.lastTickMs);
        s.lastTickMs = nowMs;

        bool ioOn = false;
        if (readIoState_(s, ioOn)) {
            if (s.actualOn != ioOn) changed = true;
            s.actualOn = ioOn;
        }

        if (s.def.tankCapacityMl <= 0.0f) {
            if (s.tankRemainingMl != 0.0f) {
                s.tankRemainingMl = 0.0f;
                changed = true;
            }
        } else if (s.tankRemainingMl > s.def.tankCapacityMl) {
            s.tankRemainingMl = s.def.tankCapacityMl;
            changed = true;
        }

        if (!s.def.enabled && s.desiredOn) {
            s.desiredOn = false;
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            changed = true;
        }

        if (s.actualOn && !dependenciesSatisfied_(i)) {
            s.desiredOn = false;
            if (writeIo_(s.ioId, false)) {
                s.actualOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            changed = true;
        }

        if (s.desiredOn && !s.actualOn) {
            if (dependenciesSatisfied_(i)) {
                if (writeIo_(s.ioId, true)) {
                    s.actualOn = true;
                    s.blockReason = POOL_DEVICE_BLOCK_NONE;
                } else {
                    s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
                }
                changed = true;
            } else {
                s.desiredOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
                changed = true;
            }
        } else if (!s.desiredOn && s.actualOn) {
            if (writeIo_(s.ioId, false)) {
                s.actualOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_NONE;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            changed = true;
        }

        if (s.actualOn && deltaMs > 0) {
            s.runningMsDay += deltaMs;
            s.runningMsWeek += deltaMs;
            s.runningMsMonth += deltaMs;
            s.runningMsTotal += deltaMs;

            // Convert L/h to ml/ms for injected volume accumulation.
            const float flowPerMs = s.def.flowLPerHour / 3600.0f;
            const float injectedDelta = flowPerMs * (float)deltaMs;
            if (injectedDelta > 0.0f) {
                s.injectedMlDay += injectedDelta;
                s.injectedMlWeek += injectedDelta;
                s.injectedMlMonth += injectedDelta;
                s.injectedMlTotal += injectedDelta;

                if (s.def.tankCapacityMl > 0.0f) {
                    s.tankRemainingMl -= injectedDelta;
                    if (s.tankRemainingMl < 0.0f) s.tankRemainingMl = 0.0f;
                }
            }
            if ((uint32_t)(nowMs - s.lastRuntimeCommitMs) >= 1000U) {
                changed = true;
            }
        }

        if (changed) {
            s.runtimeTsMs = nowMs;
            s.lastRuntimeCommitMs = nowMs;
        }

        if (dataStore_) {
            PoolDeviceRuntimeEntry rt{};
            rt.valid = true;
            rt.enabled = s.def.enabled;
            rt.desiredOn = s.desiredOn;
            rt.actualOn = s.actualOn;
            rt.type = s.def.type;
            rt.blockReason = s.blockReason;
            rt.runningSecDay = toSeconds_(s.runningMsDay);
            rt.runningSecWeek = toSeconds_(s.runningMsWeek);
            rt.runningSecMonth = toSeconds_(s.runningMsMonth);
            rt.runningSecTotal = toSeconds_(s.runningMsTotal);
            rt.injectedMlDay = s.injectedMlDay;
            rt.injectedMlWeek = s.injectedMlWeek;
            rt.injectedMlMonth = s.injectedMlMonth;
            rt.injectedMlTotal = s.injectedMlTotal;
            rt.tankRemainingMl = s.tankRemainingMl;
            rt.timestampMs = s.runtimeTsMs;
            (void)setPoolDeviceRuntime(*dataStore_, i, rt, DIRTY_SENSORS);
        }
    }
}

bool PoolDeviceModule::dependenciesSatisfied_(uint8_t slotIdx) const
{
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    const PoolDeviceSlot& s = slots_[slotIdx];
    if (!s.used) return false;
    if (s.def.dependsOnMask == 0) return true;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if ((s.def.dependsOnMask & (uint8_t)(1u << i)) == 0) continue;
        if (i == slotIdx) continue;
        const PoolDeviceSlot& dep = slots_[i];
        if (!dep.used || !dep.actualOn) return false;
    }
    return true;
}

bool PoolDeviceModule::readIoState_(const PoolDeviceSlot& slot, bool& onOut) const
{
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    uint8_t on = 0;
    if (ioSvc_->readDigital(ioSvc_->ctx, slot.ioId, &on, nullptr, nullptr) != IO_OK) return false;
    onOut = (on != 0);
    return true;
}

bool PoolDeviceModule::writeIo_(IoId ioId, bool on)
{
    if (!ioSvc_ || !ioSvc_->writeDigital) return false;
    return ioSvc_->writeDigital(ioSvc_->ctx, ioId, on ? 1U : 0U, millis()) == IO_OK;
}

uint32_t PoolDeviceModule::toSeconds_(uint64_t ms)
{
    const uint64_t sec = ms / 1000ULL;
    return (sec > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)sec;
}

void PoolDeviceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    ioSvc_ = services.get<IOServiceV2>("io");
    (void)services.add("pooldev", &poolSvc_);
    cmdSvc_ = services.get<CommandService>("cmd");
    haSvc_ = services.get<HAService>("ha");
    const EventBusService* ebSvc = services.get<EventBusService>("eventbus");
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    if (!ioSvc_) {
        LOGW("PoolDevice waiting for IOServiceV2");
    }

    if (eventBus_) {
        eventBus_->subscribe(EventId::SchedulerEventTriggered, &PoolDeviceModule::onEventStatic_, this);
    }

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        snprintf(cfgModuleName_[i], sizeof(cfgModuleName_[i]), "pdm/pd%u", (unsigned)i);
        snprintf(nvsEnabledKey_[i], sizeof(nvsEnabledKey_[i]), NvsKeys::PoolDevice::EnabledFmt, (unsigned)i);
        snprintf(nvsTypeKey_[i], sizeof(nvsTypeKey_[i]), NvsKeys::PoolDevice::TypeFmt, (unsigned)i);
        snprintf(nvsDependsKey_[i], sizeof(nvsDependsKey_[i]), NvsKeys::PoolDevice::DependsFmt, (unsigned)i);
        snprintf(nvsFlowKey_[i], sizeof(nvsFlowKey_[i]), NvsKeys::PoolDevice::FlowFmt, (unsigned)i);
        snprintf(nvsTankCapKey_[i], sizeof(nvsTankCapKey_[i]), NvsKeys::PoolDevice::TankCapFmt, (unsigned)i);
        snprintf(nvsTankInitKey_[i], sizeof(nvsTankInitKey_[i]), NvsKeys::PoolDevice::TankInitFmt, (unsigned)i);

        cfgEnabledVar_[i].nvsKey = nvsEnabledKey_[i];
        cfgEnabledVar_[i].jsonName = "enabled";
        cfgEnabledVar_[i].moduleName = cfgModuleName_[i];
        cfgEnabledVar_[i].type = ConfigType::Bool;
        cfgEnabledVar_[i].value = &s.def.enabled;
        cfgEnabledVar_[i].persistence = ConfigPersistence::Persistent;
        cfgEnabledVar_[i].size = 0;
        cfg.registerVar(cfgEnabledVar_[i]);

        cfgTypeVar_[i].nvsKey = nvsTypeKey_[i];
        cfgTypeVar_[i].jsonName = "type";
        cfgTypeVar_[i].moduleName = cfgModuleName_[i];
        cfgTypeVar_[i].type = ConfigType::UInt8;
        cfgTypeVar_[i].value = &s.def.type;
        cfgTypeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTypeVar_[i].size = 0;
        cfg.registerVar(cfgTypeVar_[i]);

        cfgDependsVar_[i].nvsKey = nvsDependsKey_[i];
        cfgDependsVar_[i].jsonName = "depends_on_mask";
        cfgDependsVar_[i].moduleName = cfgModuleName_[i];
        cfgDependsVar_[i].type = ConfigType::UInt8;
        cfgDependsVar_[i].value = &s.def.dependsOnMask;
        cfgDependsVar_[i].persistence = ConfigPersistence::Persistent;
        cfgDependsVar_[i].size = 0;
        cfg.registerVar(cfgDependsVar_[i]);

        cfgFlowVar_[i].nvsKey = nvsFlowKey_[i];
        cfgFlowVar_[i].jsonName = "flow_l_h";
        cfgFlowVar_[i].moduleName = cfgModuleName_[i];
        cfgFlowVar_[i].type = ConfigType::Float;
        cfgFlowVar_[i].value = &s.def.flowLPerHour;
        cfgFlowVar_[i].persistence = ConfigPersistence::Persistent;
        cfgFlowVar_[i].size = 0;
        cfg.registerVar(cfgFlowVar_[i]);

        cfgTankCapVar_[i].nvsKey = nvsTankCapKey_[i];
        cfgTankCapVar_[i].jsonName = "tank_capacity_ml";
        cfgTankCapVar_[i].moduleName = cfgModuleName_[i];
        cfgTankCapVar_[i].type = ConfigType::Float;
        cfgTankCapVar_[i].value = &s.def.tankCapacityMl;
        cfgTankCapVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankCapVar_[i].size = 0;
        cfg.registerVar(cfgTankCapVar_[i]);

        cfgTankInitVar_[i].nvsKey = nvsTankInitKey_[i];
        cfgTankInitVar_[i].jsonName = "tank_initial_ml";
        cfgTankInitVar_[i].moduleName = cfgModuleName_[i];
        cfgTankInitVar_[i].type = ConfigType::Float;
        cfgTankInitVar_[i].value = &s.def.tankInitialMl;
        cfgTankInitVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankInitVar_[i].size = 0;
        cfg.registerVar(cfgTankInitVar_[i]);
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.write", cmdPoolWrite_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.refill", cmdPoolRefill_, this);
    }
    if (haSvc_ && haSvc_->addNumber) {
        if (slots_[0].used) {
            const HANumberEntry n0{
                "pooldev", "pd0_flow_l_h", "Filtration Pump Flowrate",
                "cfg/pdm/pd0", "{{ value_json.flow_l_h }}",
                "cfg/set", "{\\\"pdm/pd0\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n0);
        }
        if (slots_[1].used) {
            const HANumberEntry n1{
                "pooldev", "pd1_flow_l_h", "pH Pump Flowrate",
                "cfg/pdm/pd1", "{{ value_json.flow_l_h }}",
                "cfg/set", "{\\\"pdm/pd1\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n1);
        }
        if (slots_[2].used) {
            const HANumberEntry n2{
                "pooldev", "pd2_flow_l_h", "Chlorine Pump Flowrate",
                "cfg/pdm/pd2", "{{ value_json.flow_l_h }}",
                "cfg/set", "{\\\"pdm/pd2\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n2);
        }
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    LOGI("PoolDevice module ready (devices=%u)", (unsigned)count);
    (void)logHub_;
}

void PoolDeviceModule::loop()
{
    if (!runtimeReady_) {
        if (!configureRuntime_()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            return;
        }
    }

    tickDevices_(millis());
    vTaskDelay(pdMS_TO_TICKS(200));
}
