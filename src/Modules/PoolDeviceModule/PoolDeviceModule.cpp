/**
 * @file PoolDeviceModule.cpp
 * @brief Implementation file.
 */

#include "PoolDeviceModule.h"
#include "Core/ErrorCodes.h"
#include "Core/Layout/PoolIoMap.h"
#include "Core/MqttTopics.h"
#include "Core/NvsKeys.h"
#include "Core/SystemLimits.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::PoolDeviceModule)
#include "Core/ModuleLog.h"
#include "Modules/Network/TimeModule/TimeRuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <time.h>

namespace {
static constexpr uint8_t kPoolDeviceCfgProducerId = 48;
static constexpr uint8_t kPoolDeviceCfgBranchBase = 1;
static constexpr uint8_t kPoolDeviceCfgRuntimeBranchBase = (uint8_t)(kPoolDeviceCfgBranchBase + POOL_DEVICE_MAX);
static constexpr uint8_t kTimeCfgBranch = 1;

static constexpr uint8_t poolDeviceCfgBranchFromSlot_(uint8_t slot)
{
    return (slot < POOL_DEVICE_MAX) ? (uint8_t)(kPoolDeviceCfgBranchBase + slot) : 0;
}

static constexpr uint8_t poolDeviceCfgRuntimeBranchFromSlot_(uint8_t slot)
{
    return (slot < POOL_DEVICE_MAX) ? (uint8_t)(kPoolDeviceCfgRuntimeBranchBase + slot) : 0;
}
}

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

static bool isFiniteNonNegative_(float value)
{
    return isfinite(value) && value >= 0.0f;
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
        if (s.def.maxUptimeDaySec < 0) {
            s.def.maxUptimeDaySec = 0;
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
        s.dayKey = 0;
        s.weekKey = 0;
        s.monthKey = 0;
        s.hasPersistedMetrics = false;
        s.persistDirty = false;
        s.persistImmediate = false;

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
    if (reason == POOL_DEVICE_BLOCK_MAX_UPTIME) return "max_uptime";
    return "none";
}

uint8_t PoolDeviceModule::runtimeSnapshotCount() const
{
    return (uint8_t)(activeCount_() * 2U);
}

bool PoolDeviceModule::snapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& slotIdxOut, bool& metricsOut) const
{
    uint8_t seen = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (!slots_[i].used) continue;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            metricsOut = false;
            return true;
        }
        ++seen;
        if (seen == snapshotIdx) {
            slotIdxOut = i;
            metricsOut = true;
            return true;
        }
        ++seen;
    }
    return false;
}

bool PoolDeviceModule::buildStateSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slots_[slotIdx].used) return false;

    PoolDeviceRuntimeStateEntry entry{};
    if (!poolDeviceRuntimeState(*dataStore_, slotIdx, entry)) return false;

    const char* label = deviceLabel(slotIdx);
    const char* typeStr = typeStr_(entry.type);
    const char* blockReason = blockReasonStr_(entry.blockReason);
    const int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\",\"enabled\":%s,\"desired\":%s,\"on\":%s,"
        "\"type\":\"%s\",\"block\":\"%s\",\"ts\":%lu}",
        (unsigned)slotIdx,
        (label && label[0] != '\0') ? label : "pd",
        entry.enabled ? "true" : "false",
        entry.desiredOn ? "true" : "false",
        entry.actualOn ? "true" : "false",
        typeStr,
        blockReason,
        (unsigned long)entry.tsMs
    );
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = (entry.tsMs == 0U) ? 1U : entry.tsMs;
    return true;
}

bool PoolDeviceModule::buildMetricsSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const
{
    if (!out || len == 0) return false;
    if (!dataStore_) return false;
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slots_[slotIdx].used) return false;

    PoolDeviceRuntimeMetricsEntry entry{};
    if (!poolDeviceRuntimeMetrics(*dataStore_, slotIdx, entry)) return false;

    const char* label = deviceLabel(slotIdx);
    const int wrote = snprintf(
        out, len,
        "{\"id\":\"pd%u\",\"name\":\"%s\","
        "\"running\":{\"day_s\":%lu,\"week_s\":%lu,\"month_s\":%lu,\"total_s\":%lu},"
        "\"injected\":{\"day_ml\":%.3f,\"week_ml\":%.3f,\"month_ml\":%.3f,\"total_ml\":%.3f},"
        "\"tank\":{\"remaining_ml\":%.3f},\"ts\":%lu}",
        (unsigned)slotIdx,
        (label && label[0] != '\0') ? label : "pd",
        (unsigned long)entry.runningSecDay,
        (unsigned long)entry.runningSecWeek,
        (unsigned long)entry.runningSecMonth,
        (unsigned long)entry.runningSecTotal,
        (double)entry.injectedMlDay,
        (double)entry.injectedMlWeek,
        (double)entry.injectedMlMonth,
        (double)entry.injectedMlTotal,
        (double)entry.tankRemainingMl,
        (unsigned long)entry.tsMs
    );
    if (wrote < 0 || (size_t)wrote >= len) return false;

    maxTsOut = (entry.tsMs == 0U) ? 1U : entry.tsMs;
    return true;
}

const char* PoolDeviceModule::runtimeSnapshotSuffix(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return nullptr;

    static char suffix[32];
    if (metrics) {
        snprintf(suffix, sizeof(suffix), "rt/pdm/metrics/pd%u", (unsigned)slotIdx);
    } else {
        snprintf(suffix, sizeof(suffix), "rt/pdm/state/pd%u", (unsigned)slotIdx);
    }
    return suffix;
}

RuntimeRouteClass PoolDeviceModule::runtimeSnapshotClass(uint8_t idx) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) {
        return RuntimeRouteClass::NumericThrottled;
    }
    (void)slotIdx;
    return metrics ? RuntimeRouteClass::NumericThrottled : RuntimeRouteClass::ActuatorImmediate;
}

bool PoolDeviceModule::runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return false;

    const DataKey expected = metrics
        ? (DataKey)(DATAKEY_POOL_DEVICE_METRICS_BASE + slotIdx)
        : (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + slotIdx);
    return key == expected;
}

bool PoolDeviceModule::buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const
{
    uint8_t slotIdx = 0xFF;
    bool metrics = false;
    if (!snapshotRouteFromIndex_(idx, slotIdx, metrics)) return false;
    return metrics
        ? buildMetricsSnapshot_(slotIdx, out, len, maxTsOut)
        : buildStateSnapshot_(slotIdx, out, len, maxTsOut);
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
    if (outTsMs) *outTsMs = s.stateTsMs;
    return POOLDEV_SVC_OK;
}

PoolDeviceSvcStatus PoolDeviceModule::svcWriteDesiredImpl_(uint8_t slot, uint8_t on)
{
    if (slot >= POOL_DEVICE_MAX) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    PoolDeviceSlot& s = slots_[slot];
    if (!s.used) return POOLDEV_SVC_ERR_UNKNOWN_SLOT;
    if (!runtimeReady_) return POOLDEV_SVC_ERR_NOT_READY;

    const bool requested = (on != 0U);
    const bool maxUptimeReached = maxUptimeReached_(s);
    if (requested) {
        if (!s.def.enabled) {
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            return POOLDEV_SVC_ERR_DISABLED;
        }
        if (maxUptimeReached) {
            s.blockReason = POOL_DEVICE_BLOCK_MAX_UPTIME;
            return POOLDEV_SVC_ERR_INTERLOCK;
        }
        if (!dependenciesSatisfied_(slot)) {
            s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            return POOLDEV_SVC_ERR_INTERLOCK;
        }
    }

    s.desiredOn = requested;
    if (!requested && !maxUptimeReached) s.blockReason = POOL_DEVICE_BLOCK_NONE;

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
    s.forceMetricsCommit = true;
    s.persistDirty = true;
    s.persistImmediate = true;
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
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingArgs);
        return false;
    }

    if (!args.containsKey("slot")) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingSlot);
        return false;
    }
    if (!args["slot"].is<uint8_t>()) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::BadSlot);
        return false;
    }
    const uint8_t slot = args["slot"].as<uint8_t>();
    if (slot >= POOL_DEVICE_MAX) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::BadSlot);
        return false;
    }

    if (!args.containsKey("value")) {
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingValue);
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
        writeCmdError_(reply, replyLen, "pooldevice.write", ErrorCode::MissingValue);
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
        writeCmdErrorSlot_(reply, replyLen, "pooldevice.write", code, slot);
        return false;
    }

    const char* label = deviceLabel(slot);
    LOGI("Manual %s %s (slot=%u)",
         requested ? "Start" : "Stop",
         (label && label[0] != '\0') ? label : "Pool Device",
         (unsigned)slot);
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

void PoolDeviceModule::requestPeriodReconcile_()
{
    portENTER_CRITICAL(&resetMux_);
    periodReconcilePending_ = true;
    portEXIT_CRITICAL(&resetMux_);
}

bool PoolDeviceModule::currentPeriodKeys_(PeriodKeys& out) const
{
    if (!dataStore_) return false;
    if (!timeReady(*dataStore_)) return false;

    time_t now = 0;
    time(&now);
    if (now < (time_t)MIN_VALID_EPOCH_SEC) return false;

    struct tm localNow{};
    if (!localtime_r(&now, &localNow)) return false;

    const uint32_t year = (uint32_t)(localNow.tm_year + 1900);
    const uint32_t month = (uint32_t)(localNow.tm_mon + 1);
    const uint32_t day = (uint32_t)localNow.tm_mday;
    out.day = (year * 10000UL) + (month * 100UL) + day;
    out.month = (year * 100UL) + month;

    const bool weekStartMonday = weekStartMondayFromConfig_();
    const int weekOffset = weekStartMonday ? ((localNow.tm_wday + 6) % 7) : localNow.tm_wday;

    struct tm weekStart = localNow;
    weekStart.tm_hour = 12;
    weekStart.tm_min = 0;
    weekStart.tm_sec = 0;
    weekStart.tm_mday -= weekOffset;
    time_t weekEpoch = mktime(&weekStart);
    if (weekEpoch < (time_t)MIN_VALID_EPOCH_SEC) return false;

    struct tm weekTm{};
    if (!localtime_r(&weekEpoch, &weekTm)) return false;

    const uint32_t weekYear = (uint32_t)(weekTm.tm_year + 1900);
    const uint32_t weekMonth = (uint32_t)(weekTm.tm_mon + 1);
    const uint32_t weekDay = (uint32_t)weekTm.tm_mday;
    out.week = (weekYear * 10000UL) + (weekMonth * 100UL) + weekDay;
    return true;
}

bool PoolDeviceModule::weekStartMondayFromConfig_() const
{
    if (!cfgStore_) return true;

    char timeCfg[160] = {0};
    bool truncated = false;
    if (!cfgStore_->toJsonModule("time", timeCfg, sizeof(timeCfg), &truncated)) return true;
    if (truncated) return true;

    StaticJsonDocument<192> doc;
    const DeserializationError err = deserializeJson(doc, timeCfg);
    if (err || !doc.is<JsonObjectConst>()) return true;
    const JsonVariantConst v = doc["week_start_mon"];
    if (v.is<bool>()) return v.as<bool>();
    if (v.is<int32_t>()) return v.as<int32_t>() != 0;
    if (v.is<uint32_t>()) return v.as<uint32_t>() != 0U;
    return true;
}

bool PoolDeviceModule::loadPersistedMetrics_(uint8_t slotIdx, PoolDeviceSlot& slot)
{
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slot.used) return false;
    if (runtimePersistBuf_[slotIdx][0] == '\0') return false;

    char parseBuf[sizeof(runtimePersistBuf_[0])] = {0};
    strncpy(parseBuf, runtimePersistBuf_[slotIdx], sizeof(parseBuf) - 1);
    parseBuf[sizeof(parseBuf) - 1] = '\0';

    char* save = nullptr;
    auto nextTok = [&save](char* s) -> char* { return strtok_r(s, ",", &save); };

    char* tok = nextTok(parseBuf);
    if (!tok || strcmp(tok, "v1") != 0) {
        LOGW("Pool device %s runtime blob invalid header", slot.id);
        return false;
    }

    auto parseU64 = [&](uint64_t& out) -> bool {
        char* t = nextTok(nullptr);
        if (!t || t[0] == '\0') return false;
        char* end = nullptr;
        out = strtoull(t, &end, 10);
        return end && *end == '\0';
    };
    auto parseU32 = [&](uint32_t& out) -> bool {
        char* t = nextTok(nullptr);
        if (!t || t[0] == '\0') return false;
        char* end = nullptr;
        out = (uint32_t)strtoul(t, &end, 10);
        return end && *end == '\0';
    };
    auto parseF32 = [&](float& out) -> bool {
        char* t = nextTok(nullptr);
        if (!t || t[0] == '\0') return false;
        char* end = nullptr;
        out = strtof(t, &end);
        return end && *end == '\0';
    };

    uint64_t runningDay = 0ULL;
    uint64_t runningWeek = 0ULL;
    uint64_t runningMonth = 0ULL;
    uint64_t runningTotal = 0ULL;
    float injectedDay = 0.0f;
    float injectedWeek = 0.0f;
    float injectedMonth = 0.0f;
    float injectedTotal = 0.0f;
    float tank = 0.0f;
    uint32_t dayKey = 0U;
    uint32_t weekKey = 0U;
    uint32_t monthKey = 0U;

    if (!parseU64(runningDay) ||
        !parseU64(runningWeek) ||
        !parseU64(runningMonth) ||
        !parseU64(runningTotal) ||
        !parseF32(injectedDay) ||
        !parseF32(injectedWeek) ||
        !parseF32(injectedMonth) ||
        !parseF32(injectedTotal) ||
        !parseF32(tank) ||
        !parseU32(dayKey) ||
        !parseU32(weekKey) ||
        !parseU32(monthKey)) {
        LOGW("Pool device %s runtime blob parse failed", slot.id);
        return false;
    }

    if (nextTok(nullptr) != nullptr) {
        LOGW("Pool device %s runtime blob has trailing fields", slot.id);
        return false;
    }

    slot.runningMsDay = runningDay;
    slot.runningMsWeek = runningWeek;
    slot.runningMsMonth = runningMonth;
    slot.runningMsTotal = runningTotal;
    slot.injectedMlDay = isFiniteNonNegative_(injectedDay) ? injectedDay : 0.0f;
    slot.injectedMlWeek = isFiniteNonNegative_(injectedWeek) ? injectedWeek : 0.0f;
    slot.injectedMlMonth = isFiniteNonNegative_(injectedMonth) ? injectedMonth : 0.0f;
    slot.injectedMlTotal = isFiniteNonNegative_(injectedTotal) ? injectedTotal : 0.0f;
    slot.tankRemainingMl = isFiniteNonNegative_(tank) ? tank : 0.0f;
    slot.dayKey = dayKey;
    slot.weekKey = weekKey;
    slot.monthKey = monthKey;
    slot.hasPersistedMetrics = true;
    slot.persistDirty = false;
    slot.persistImmediate = false;
    return true;
}

bool PoolDeviceModule::persistMetrics_(uint8_t slotIdx, PoolDeviceSlot& slot, uint32_t nowMs)
{
    if (slotIdx >= POOL_DEVICE_MAX) return false;
    if (!slot.used) return false;
    if (!cfgStore_) return false;

    char encoded[sizeof(runtimePersistBuf_[0])] = {0};
    const int wrote = snprintf(
        encoded, sizeof(encoded),
        "v1,%llu,%llu,%llu,%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%lu,%lu,%lu",
        (unsigned long long)slot.runningMsDay,
        (unsigned long long)slot.runningMsWeek,
        (unsigned long long)slot.runningMsMonth,
        (unsigned long long)slot.runningMsTotal,
        (double)(isFiniteNonNegative_(slot.injectedMlDay) ? slot.injectedMlDay : 0.0f),
        (double)(isFiniteNonNegative_(slot.injectedMlWeek) ? slot.injectedMlWeek : 0.0f),
        (double)(isFiniteNonNegative_(slot.injectedMlMonth) ? slot.injectedMlMonth : 0.0f),
        (double)(isFiniteNonNegative_(slot.injectedMlTotal) ? slot.injectedMlTotal : 0.0f),
        (double)(isFiniteNonNegative_(slot.tankRemainingMl) ? slot.tankRemainingMl : 0.0f),
        (unsigned long)slot.dayKey,
        (unsigned long)slot.weekKey,
        (unsigned long)slot.monthKey);
    if (wrote <= 0 || (size_t)wrote >= sizeof(encoded)) {
        LOGW("Pool device %s runtime persist failed", slot.id);
        return false;
    }
    if (!cfgStore_->set(cfgRuntimeVar_[slotIdx], encoded)) return false;

    slot.lastPersistMs = nowMs;
    slot.persistDirty = false;
    slot.persistImmediate = false;
    slot.hasPersistedMetrics = true;
    return true;
}

bool PoolDeviceModule::reconcilePeriodCountersFromClock_()
{
    PeriodKeys keys{};
    if (!currentPeriodKeys_(keys)) return false;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        bool changed = false;
        if (s.dayKey == 0U) {
            s.dayKey = keys.day;
            changed = true;
        } else if (s.dayKey != keys.day) {
            s.runningMsDay = 0;
            s.injectedMlDay = 0.0f;
            s.dayKey = keys.day;
            changed = true;
        }
        if (s.weekKey == 0U) {
            s.weekKey = keys.week;
            changed = true;
        } else if (s.weekKey != keys.week) {
            s.runningMsWeek = 0;
            s.injectedMlWeek = 0.0f;
            s.weekKey = keys.week;
            changed = true;
        }
        if (s.monthKey == 0U) {
            s.monthKey = keys.month;
            changed = true;
        } else if (s.monthKey != keys.month) {
            s.runningMsMonth = 0;
            s.injectedMlMonth = 0.0f;
            s.monthKey = keys.month;
            changed = true;
        }

        if (changed) {
            s.forceMetricsCommit = true;
            s.persistDirty = true;
            s.persistImmediate = true;
        }
    }

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

        if (s.def.tankCapacityMl <= 0.0f) {
            s.tankRemainingMl = 0.0f;
        } else if (!s.hasPersistedMetrics) {
            float initial = (s.def.tankInitialMl > 0.0f) ? s.def.tankInitialMl : s.def.tankCapacityMl;
            if (initial > s.def.tankCapacityMl) initial = s.def.tankCapacityMl;
            if (initial < 0.0f) initial = 0.0f;
            s.tankRemainingMl = initial;
        } else {
            if (!isFiniteNonNegative_(s.tankRemainingMl)) s.tankRemainingMl = 0.0f;
            if (s.tankRemainingMl > s.def.tankCapacityMl) s.tankRemainingMl = s.def.tankCapacityMl;
        }

        s.lastTickMs = now;
        s.stateTsMs = now;
        s.metricsTsMs = now;
        s.lastRuntimeCommitMs = now;
        s.lastPersistMs = now;

        PoolDeviceRuntimeStateEntry rtState{};
        rtState.valid = true;
        rtState.enabled = s.def.enabled;
        rtState.desiredOn = s.desiredOn;
        rtState.actualOn = s.actualOn;
        rtState.type = s.def.type;
        rtState.blockReason = s.blockReason;
        rtState.tsMs = s.stateTsMs;

        PoolDeviceRuntimeMetricsEntry rtMetrics{};
        rtMetrics.valid = true;
        rtMetrics.runningSecDay = toSeconds_(s.runningMsDay);
        rtMetrics.runningSecWeek = toSeconds_(s.runningMsWeek);
        rtMetrics.runningSecMonth = toSeconds_(s.runningMsMonth);
        rtMetrics.runningSecTotal = toSeconds_(s.runningMsTotal);
        rtMetrics.injectedMlDay = s.injectedMlDay;
        rtMetrics.injectedMlWeek = s.injectedMlWeek;
        rtMetrics.injectedMlMonth = s.injectedMlMonth;
        rtMetrics.injectedMlTotal = s.injectedMlTotal;
        rtMetrics.tankRemainingMl = s.tankRemainingMl;
        rtMetrics.tsMs = s.metricsTsMs;
        if (dataStore_) {
            (void)setPoolDeviceRuntimeState(*dataStore_, i, rtState);
            (void)setPoolDeviceRuntimeMetrics(*dataStore_, i, rtMetrics);
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
    if (e.id == EventId::DataChanged) {
        if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
        const DataChangedPayload* p = (const DataChangedPayload*)e.payload;
        if (p->id != DATAKEY_TIME_READY) return;
        if (!dataStore_) return;
        if (timeReady(*dataStore_)) requestPeriodReconcile_();
        return;
    }

    if (e.id == EventId::ConfigChanged) {
        if (!e.payload || e.len < sizeof(ConfigChangedPayload)) return;
        const ConfigChangedPayload* p = (const ConfigChangedPayload*)e.payload;
        if (p->moduleId == (uint8_t)ConfigModuleId::Time &&
            p->localBranchId == kTimeCfgBranch) {
            requestPeriodReconcile_();
        }
        return;
    }

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
    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsDay = 0;
        s.injectedMlDay = 0.0f;
        if (hasKeys) s.dayKey = keys.day;
        s.forceMetricsCommit = true;
        s.persistDirty = true;
        s.persistImmediate = true;
    }
}

void PoolDeviceModule::resetWeeklyCounters_()
{
    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsWeek = 0;
        s.injectedMlWeek = 0.0f;
        if (hasKeys) s.weekKey = keys.week;
        s.forceMetricsCommit = true;
        s.persistDirty = true;
        s.persistImmediate = true;
    }
}

void PoolDeviceModule::resetMonthlyCounters_()
{
    PeriodKeys keys{};
    const bool hasKeys = currentPeriodKeys_(keys);
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        s.runningMsMonth = 0;
        s.injectedMlMonth = 0.0f;
        if (hasKeys) s.monthKey = keys.month;
        s.forceMetricsCommit = true;
        s.persistDirty = true;
        s.persistImmediate = true;
    }
}

void PoolDeviceModule::tickDevices_(uint32_t nowMs)
{
    uint8_t pending = 0;
    bool reconcilePending = false;
    portENTER_CRITICAL(&resetMux_);
    pending = resetPendingMask_;
    resetPendingMask_ = 0;
    reconcilePending = periodReconcilePending_;
    periodReconcilePending_ = false;
    portEXIT_CRITICAL(&resetMux_);

    if (pending & RESET_PENDING_DAY) resetDailyCounters_();
    if (pending & RESET_PENDING_WEEK) resetWeeklyCounters_();
    if (pending & RESET_PENDING_MONTH) resetMonthlyCounters_();
    if (reconcilePending && !reconcilePeriodCountersFromClock_()) {
        requestPeriodReconcile_();
    }

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        const bool wasActualOn = s.actualOn;
        bool stateChanged = false;
        bool metricsChanged = (pending != 0U) || s.forceMetricsCommit;
        s.forceMetricsCommit = false;

        if (s.lastTickMs == 0) s.lastTickMs = nowMs;
        const uint32_t deltaMs = (uint32_t)(nowMs - s.lastTickMs);
        s.lastTickMs = nowMs;

        bool ioOn = false;
        if (readIoState_(s, ioOn)) {
            if (s.actualOn != ioOn) stateChanged = true;
            s.actualOn = ioOn;
        }

        if (s.def.tankCapacityMl <= 0.0f) {
            if (s.tankRemainingMl != 0.0f) {
                s.tankRemainingMl = 0.0f;
                metricsChanged = true;
            }
        } else if (s.tankRemainingMl > s.def.tankCapacityMl) {
            s.tankRemainingMl = s.def.tankCapacityMl;
            metricsChanged = true;
        }

        if (!s.def.enabled && s.desiredOn) {
            s.desiredOn = false;
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            stateChanged = true;
        }

        const bool maxUptimeReached = maxUptimeReached_(s);
        if (s.def.enabled && maxUptimeReached) {
            if (s.desiredOn) {
                s.desiredOn = false;
                stateChanged = true;
            }
            if (s.actualOn) {
                if (writeIo_(s.ioId, false)) {
                    s.actualOn = false;
                    s.blockReason = POOL_DEVICE_BLOCK_MAX_UPTIME;
                } else {
                    s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
                }
                stateChanged = true;
            } else if (s.blockReason != POOL_DEVICE_BLOCK_IO_ERROR &&
                       s.blockReason != POOL_DEVICE_BLOCK_MAX_UPTIME) {
                s.blockReason = POOL_DEVICE_BLOCK_MAX_UPTIME;
                stateChanged = true;
            }
        } else if (s.blockReason == POOL_DEVICE_BLOCK_MAX_UPTIME) {
            s.blockReason = POOL_DEVICE_BLOCK_NONE;
            stateChanged = true;
        }

        if (s.actualOn && !dependenciesSatisfied_(i)) {
            s.desiredOn = false;
            if (writeIo_(s.ioId, false)) {
                s.actualOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            stateChanged = true;
        }

        if (s.desiredOn && !s.actualOn) {
            if (dependenciesSatisfied_(i)) {
                if (writeIo_(s.ioId, true)) {
                    s.actualOn = true;
                    s.blockReason = POOL_DEVICE_BLOCK_NONE;
                } else {
                    s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
                }
                stateChanged = true;
            } else {
                s.desiredOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
                stateChanged = true;
            }
        } else if (!s.desiredOn && s.actualOn) {
            if (writeIo_(s.ioId, false)) {
                s.actualOn = false;
                s.blockReason = maxUptimeReached_(s) ? POOL_DEVICE_BLOCK_MAX_UPTIME : POOL_DEVICE_BLOCK_NONE;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            stateChanged = true;
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
                metricsChanged = true;
            }
        }
        if (wasActualOn && !s.actualOn) {
            s.persistDirty = true;
            s.persistImmediate = true;
        }

        if (dataStore_) {
            PoolDeviceRuntimeStateEntry prevState{};
            if (poolDeviceRuntimeState(*dataStore_, i, prevState) && prevState.valid) {
                if ((prevState.enabled != s.def.enabled) ||
                    (prevState.desiredOn != s.desiredOn) ||
                    (prevState.actualOn != s.actualOn) ||
                    (prevState.type != s.def.type) ||
                    (prevState.blockReason != s.blockReason)) {
                    stateChanged = true;
                }
            } else {
                stateChanged = true;
            }
        }

        if (stateChanged) {
            s.stateTsMs = nowMs;
        }
        if (metricsChanged) {
            s.metricsTsMs = nowMs;
            s.lastRuntimeCommitMs = nowMs;
            s.persistDirty = true;
        }

        if (dataStore_) {
            PoolDeviceRuntimeStateEntry rtState{};
            rtState.valid = true;
            rtState.enabled = s.def.enabled;
            rtState.desiredOn = s.desiredOn;
            rtState.actualOn = s.actualOn;
            rtState.type = s.def.type;
            rtState.blockReason = s.blockReason;
            rtState.tsMs = s.stateTsMs;
            (void)setPoolDeviceRuntimeState(*dataStore_, i, rtState);

            if (metricsChanged) {
                PoolDeviceRuntimeMetricsEntry rtMetrics{};
                rtMetrics.valid = true;
                rtMetrics.runningSecDay = toSeconds_(s.runningMsDay);
                rtMetrics.runningSecWeek = toSeconds_(s.runningMsWeek);
                rtMetrics.runningSecMonth = toSeconds_(s.runningMsMonth);
                rtMetrics.runningSecTotal = toSeconds_(s.runningMsTotal);
                rtMetrics.injectedMlDay = s.injectedMlDay;
                rtMetrics.injectedMlWeek = s.injectedMlWeek;
                rtMetrics.injectedMlMonth = s.injectedMlMonth;
                rtMetrics.injectedMlTotal = s.injectedMlTotal;
                rtMetrics.tankRemainingMl = s.tankRemainingMl;
                rtMetrics.tsMs = s.metricsTsMs;
                (void)setPoolDeviceRuntimeMetrics(*dataStore_, i, rtMetrics);
            }
        }

        bool shouldPersist = false;
        if (s.persistDirty) {
            if (s.persistImmediate) {
                shouldPersist = true;
            } else if (!s.actualOn) {
                shouldPersist = true;
            } else if ((uint32_t)(nowMs - s.lastPersistMs) >= RUNTIME_PERSIST_INTERVAL_MS) {
                shouldPersist = true;
            }
        }
        if (shouldPersist) {
            (void)persistMetrics_(i, s, nowMs);
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

bool PoolDeviceModule::maxUptimeReached_(const PoolDeviceSlot& slot)
{
    if (slot.def.maxUptimeDaySec <= 0) return false;
    const uint64_t limitMs = (uint64_t)(uint32_t)slot.def.maxUptimeDaySec * 1000ULL;
    return slot.runningMsDay >= limitMs;
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
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::PoolDevice;
    const uint8_t cfgRouteCap = (uint8_t)(POOL_DEVICE_MAX * 2U);
    cfgStore_ = &cfg;
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
        eventBus_->subscribe(EventId::DataChanged, &PoolDeviceModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::ConfigChanged, &PoolDeviceModule::onEventStatic_, this);
    }

    if (!cfgRoutes_) {
        cfgRoutes_ = new (std::nothrow) MqttConfigRouteProducer::Route[cfgRouteCap];
    }
    cfgRouteCount_ = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        const uint8_t localBranchId = poolDeviceCfgBranchFromSlot_(i);
        const uint8_t runtimeBranchId = poolDeviceCfgRuntimeBranchFromSlot_(i);

        snprintf(cfgModuleName_[i], sizeof(cfgModuleName_[i]), "pdm/pd%u", (unsigned)i);
        snprintf(nvsEnabledKey_[i], sizeof(nvsEnabledKey_[i]), NvsKeys::PoolDevice::EnabledFmt, (unsigned)i);
        snprintf(nvsTypeKey_[i], sizeof(nvsTypeKey_[i]), NvsKeys::PoolDevice::TypeFmt, (unsigned)i);
        snprintf(nvsDependsKey_[i], sizeof(nvsDependsKey_[i]), NvsKeys::PoolDevice::DependsFmt, (unsigned)i);
        snprintf(nvsFlowKey_[i], sizeof(nvsFlowKey_[i]), NvsKeys::PoolDevice::FlowFmt, (unsigned)i);
        snprintf(nvsTankCapKey_[i], sizeof(nvsTankCapKey_[i]), NvsKeys::PoolDevice::TankCapFmt, (unsigned)i);
        snprintf(nvsTankInitKey_[i], sizeof(nvsTankInitKey_[i]), NvsKeys::PoolDevice::TankInitFmt, (unsigned)i);
        snprintf(nvsMaxUptimeKey_[i], sizeof(nvsMaxUptimeKey_[i]), NvsKeys::PoolDevice::MaxUptimeFmt, (unsigned)i);
        snprintf(nvsRuntimeKey_[i], sizeof(nvsRuntimeKey_[i]), NvsKeys::PoolDevice::RuntimeFmt, (unsigned)i);
        snprintf(cfgRuntimeModuleName_[i], sizeof(cfgRuntimeModuleName_[i]), "pdmrt/pd%u", (unsigned)i);

        if (cfgRoutes_ && cfgRouteCount_ < cfgRouteCap) {
            MqttConfigRouteProducer::Route& cfgRoute = cfgRoutes_[cfgRouteCount_++];
            cfgRoute = MqttConfigRouteProducer::Route{};
            cfgRoute.messageId = (uint16_t)(1U + i);
            cfgRoute.branch = {(uint8_t)ConfigModuleId::PoolDevice, localBranchId};
            cfgRoute.moduleName = cfgModuleName_[i];
            cfgRoute.topicSuffix = cfgModuleName_[i];
            cfgRoute.changePriority = (uint8_t)MqttPublishPriority::Normal;
        }
        if (cfgRoutes_ && cfgRouteCount_ < cfgRouteCap) {
            MqttConfigRouteProducer::Route& cfgRuntimeRoute = cfgRoutes_[cfgRouteCount_++];
            cfgRuntimeRoute = MqttConfigRouteProducer::Route{};
            cfgRuntimeRoute.messageId = (uint16_t)(1U + POOL_DEVICE_MAX + i);
            cfgRuntimeRoute.branch = {(uint8_t)ConfigModuleId::PoolDevice, runtimeBranchId};
            cfgRuntimeRoute.moduleName = cfgRuntimeModuleName_[i];
            cfgRuntimeRoute.topicSuffix = cfgRuntimeModuleName_[i];
            cfgRuntimeRoute.changePriority = (uint8_t)MqttPublishPriority::Normal;
        }

        cfgEnabledVar_[i].nvsKey = nvsEnabledKey_[i];
        cfgEnabledVar_[i].jsonName = "enabled";
        cfgEnabledVar_[i].moduleName = cfgModuleName_[i];
        cfgEnabledVar_[i].type = ConfigType::Bool;
        cfgEnabledVar_[i].value = &s.def.enabled;
        cfgEnabledVar_[i].persistence = ConfigPersistence::Persistent;
        cfgEnabledVar_[i].size = 0;
        cfg.registerVar(cfgEnabledVar_[i], kCfgModuleId, localBranchId);

        cfgTypeVar_[i].nvsKey = nvsTypeKey_[i];
        cfgTypeVar_[i].jsonName = "type";
        cfgTypeVar_[i].moduleName = cfgModuleName_[i];
        cfgTypeVar_[i].type = ConfigType::UInt8;
        cfgTypeVar_[i].value = &s.def.type;
        cfgTypeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTypeVar_[i].size = 0;
        cfg.registerVar(cfgTypeVar_[i], kCfgModuleId, localBranchId);

        cfgDependsVar_[i].nvsKey = nvsDependsKey_[i];
        cfgDependsVar_[i].jsonName = "depends_on_mask";
        cfgDependsVar_[i].moduleName = cfgModuleName_[i];
        cfgDependsVar_[i].type = ConfigType::UInt8;
        cfgDependsVar_[i].value = &s.def.dependsOnMask;
        cfgDependsVar_[i].persistence = ConfigPersistence::Persistent;
        cfgDependsVar_[i].size = 0;
        cfg.registerVar(cfgDependsVar_[i], kCfgModuleId, localBranchId);

        cfgFlowVar_[i].nvsKey = nvsFlowKey_[i];
        cfgFlowVar_[i].jsonName = "flow_l_h";
        cfgFlowVar_[i].moduleName = cfgModuleName_[i];
        cfgFlowVar_[i].type = ConfigType::Float;
        cfgFlowVar_[i].value = &s.def.flowLPerHour;
        cfgFlowVar_[i].persistence = ConfigPersistence::Persistent;
        cfgFlowVar_[i].size = 0;
        cfg.registerVar(cfgFlowVar_[i], kCfgModuleId, localBranchId);

        cfgTankCapVar_[i].nvsKey = nvsTankCapKey_[i];
        cfgTankCapVar_[i].jsonName = "tank_cap_ml";
        cfgTankCapVar_[i].moduleName = cfgModuleName_[i];
        cfgTankCapVar_[i].type = ConfigType::Float;
        cfgTankCapVar_[i].value = &s.def.tankCapacityMl;
        cfgTankCapVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankCapVar_[i].size = 0;
        cfg.registerVar(cfgTankCapVar_[i], kCfgModuleId, localBranchId);

        cfgTankInitVar_[i].nvsKey = nvsTankInitKey_[i];
        cfgTankInitVar_[i].jsonName = "tank_init_ml";
        cfgTankInitVar_[i].moduleName = cfgModuleName_[i];
        cfgTankInitVar_[i].type = ConfigType::Float;
        cfgTankInitVar_[i].value = &s.def.tankInitialMl;
        cfgTankInitVar_[i].persistence = ConfigPersistence::Persistent;
        cfgTankInitVar_[i].size = 0;
        cfg.registerVar(cfgTankInitVar_[i], kCfgModuleId, localBranchId);

        cfgMaxUptimeVar_[i].nvsKey = nvsMaxUptimeKey_[i];
        cfgMaxUptimeVar_[i].jsonName = "max_uptime_day_s";
        cfgMaxUptimeVar_[i].moduleName = cfgModuleName_[i];
        cfgMaxUptimeVar_[i].type = ConfigType::Int32;
        cfgMaxUptimeVar_[i].value = &s.def.maxUptimeDaySec;
        cfgMaxUptimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgMaxUptimeVar_[i].size = 0;
        cfg.registerVar(cfgMaxUptimeVar_[i], kCfgModuleId, localBranchId);

        cfgRuntimeVar_[i].nvsKey = nvsRuntimeKey_[i];
        cfgRuntimeVar_[i].jsonName = "metrics_blob";
        cfgRuntimeVar_[i].moduleName = cfgRuntimeModuleName_[i];
        cfgRuntimeVar_[i].type = ConfigType::CharArray;
        cfgRuntimeVar_[i].value = runtimePersistBuf_[i];
        cfgRuntimeVar_[i].persistence = ConfigPersistence::Persistent;
        cfgRuntimeVar_[i].size = sizeof(runtimePersistBuf_[i]);
        cfg.registerVar(cfgRuntimeVar_[i], kCfgModuleId, runtimeBranchId);
    }

    if (cmdSvc_ && cmdSvc_->registerHandler) {
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pooldevice.write", cmdPoolWrite_, this);
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.write", cmdPoolWrite_, this); // backward compatibility
        cmdSvc_->registerHandler(cmdSvc_->ctx, "pool.refill", cmdPoolRefill_, this);
    }
    if (haSvc_ && haSvc_->addSensor) {
        if (slots_[POOL_IO_SLOT_CHLORINE_PUMP].used) {
            const HASensorEntry s0{
                "pooldev", "chlorine_pump_uptime_s", "Pump uptime Chlorine",
                "rt/pdm/metrics/pd2", "{{ value_json.running.day_s | int(0) }}",
                nullptr, "mdi:timer-outline", "s"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s0);
            const HASensorEntry s0b{
                "pooldev", "chlorine_tank_remaining_l", "Tank remaining Chlorine",
                "rt/pdm/metrics/pd2", "{{ ((value_json.tank.remaining_ml | float(0)) / 1000) | round(2) }}",
                nullptr, "mdi:water-check", "L"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s0b);
        }
        if (slots_[POOL_IO_SLOT_PH_PUMP].used) {
            const HASensorEntry s1{
                "pooldev", "ph_pump_uptime_s", "Pump uptime pH",
                "rt/pdm/metrics/pd1", "{{ value_json.running.day_s | int(0) }}",
                nullptr, "mdi:timer-outline", "s"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s1);
            const HASensorEntry s1b{
                "pooldev", "ph_tank_remaining_l", "Tank remaining pH",
                "rt/pdm/metrics/pd1", "{{ ((value_json.tank.remaining_ml | float(0)) / 1000) | round(2) }}",
                nullptr, "mdi:beaker-check-outline", "L", false
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s1b);
        }
        if (slots_[POOL_IO_SLOT_FILL_PUMP].used) {
            const HASensorEntry s2{
                "pooldev", "fill_pump_uptime_mn", "Pump uptime Fill",
                "rt/pdm/metrics/pd4", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s2);
        }
        if (slots_[POOL_IO_SLOT_FILTRATION_PUMP].used) {
            const HASensorEntry s3{
                "pooldev", "filtration_uptime_mn", "Pump uptime Filtration",
                "rt/pdm/metrics/pd0", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s3);
        }
        if (slots_[POOL_IO_SLOT_CHLORINE_GENERATOR].used) {
            const HASensorEntry s4{
                "pooldev", "chlorine_generator_uptime_mn", "Pump uptime Chlorine Generator",
                "rt/pdm/metrics/pd5", "{{ ((value_json.running.day_s | float(0)) / 60) | round(0) | int(0) }}",
                nullptr, "mdi:timer-outline", "mn"
            };
            (void)haSvc_->addSensor(haSvc_->ctx, &s4);
        }
    }

    if (haSvc_ && haSvc_->addNumber) {
        if (slots_[0].used) {
            const HANumberEntry n0{
                "pooldev", "pd0_flow_l_h", "Filtration Pump Flowrate",
                "cfg/pdm/pd0", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd0\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n0);
        }
        if (slots_[1].used) {
            const HANumberEntry n1{
                "pooldev", "pd1_flow_l_h", "pH Pump Flowrate",
                "cfg/pdm/pd1", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd1\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n1);
        }
        if (slots_[2].used) {
            const HANumberEntry n2{
                "pooldev", "pd2_flow_l_h", "Chlorine Pump Flowrate",
                "cfg/pdm/pd2", "{{ value_json.flow_l_h }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd2\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}",
                0.0f, 3.0f, 0.1f, "slider", "config", "mdi:water-sync", "L/h"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n2);
        }
        if (slots_[POOL_IO_SLOT_PH_PUMP].used) {
            const HANumberEntry n3{
                "pooldev", "pd1_max_uptime_min", "Max uptime pH Pump",
                "cfg/pdm/pd1", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd1\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "slider", "config", "mdi:timer-cog-outline", "min"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n3);
        }
        if (slots_[POOL_IO_SLOT_CHLORINE_PUMP].used) {
            const HANumberEntry n4{
                "pooldev", "pd2_max_uptime_min", "Max uptime Chlorine Pump",
                "cfg/pdm/pd2", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd2\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "slider", "config", "mdi:timer-cog-outline", "min"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n4);
        }
        if (slots_[POOL_IO_SLOT_CHLORINE_GENERATOR].used) {
            const HANumberEntry n5{
                "pooldev", "pd5_max_uptime_min", "Max uptime Chlorine Generator",
                "cfg/pdm/pd5", "{{ ((value_json.max_uptime_day_s | float(0)) / 60) | round(0) | int(0) }}",
                MqttTopics::SuffixCfgSet, "{\\\"pdm/pd5\\\":{\\\"max_uptime_day_s\\\":{{ (value | float(0) * 60) | round(0) | int(0) }}}}",
                0.0f, 1440.0f, 1.0f, "slider", "config", "mdi:timer-cog-outline", "min"
            };
            (void)haSvc_->addNumber(haSvc_->ctx, &n5);
        }
    }
    if (haSvc_ && haSvc_->addButton) {
        if (slots_[POOL_IO_SLOT_PH_PUMP].used) {
            const HAButtonEntry refillPhTank{
                "pooldev",
                "refill_ph_tank",
                "Fill pH Tank",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.refill\\\",\\\"args\\\":{\\\"slot\\\":1}}",
                "config",
                "mdi:beaker-plus-outline"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &refillPhTank);
        }
        if (slots_[POOL_IO_SLOT_CHLORINE_PUMP].used) {
            const HAButtonEntry refillChlorineTank{
                "pooldev",
                "refill_chlorine_tank",
                "Fill Chlorine Tank",
                MqttTopics::SuffixCmd,
                "{\\\"cmd\\\":\\\"pool.refill\\\",\\\"args\\\":{\\\"slot\\\":2}}",
                "config",
                "mdi:water-plus"
            };
            (void)haSvc_->addButton(haSvc_->ctx, &refillChlorineTank);
        }
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        if (slots_[i].used) ++count;
    }
    LOGI("PoolDevice module ready (devices=%u)", (unsigned)count);
    (void)logHub_;
}

void PoolDeviceModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_ && cfgRoutes_ && cfgRouteCount_ > 0U) {
        cfgMqttPub_->configure(this,
                               kPoolDeviceCfgProducerId,
                               cfgRoutes_,
                               cfgRouteCount_,
                               services);
    }

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        (void)loadPersistedMetrics_(i, s);
    }
    requestPeriodReconcile_();
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
