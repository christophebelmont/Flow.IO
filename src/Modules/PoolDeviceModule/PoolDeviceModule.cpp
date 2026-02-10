/**
 * @file PoolDeviceModule.cpp
 * @brief Implementation file.
 */

#include "PoolDeviceModule.h"
#define LOG_TAG "PoolDevc"
#include "Core/ModuleLog.h"
#include "Modules/IOModule/IOModuleDataModel.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char* findJsonStringValueLocal(const char* json, const char* key)
{
    static char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(json ? json : "", pat);
    if (!p) return nullptr;
    return p + strlen(pat);
}

static const char* findJsonValueLocal(const char* json, const char* key)
{
    static char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json ? json : "", pat);
    if (!p) return nullptr;
    return p + strlen(pat);
}

static const char* skipWsLocal(const char* p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
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
        if (s.def.ioId[0] == '\0') {
            snprintf(s.def.ioId, sizeof(s.def.ioId), "d%u", (unsigned)i);
        }

        uint8_t ioIdx = 0xFF;
        if (!parseDigitalIoId_(s.def.ioId, ioIdx)) {
            s.used = false;
            return false;
        }
        s.ioIdx = ioIdx;
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
        "\"running\":{\"day_s\":%lu,\"month_s\":%lu,\"year_s\":%lu,\"total_s\":%lu},"
        "\"injected\":{\"day_ml\":%.3f,\"month_ml\":%.3f,\"year_ml\":%.3f,\"total_ml\":%.3f},"
        "\"tank\":{\"remaining_ml\":%.3f},\"ts\":%lu}",
        (unsigned)slotIdx,
        (label && label[0] != '\0') ? label : "pd",
        entry.enabled ? "true" : "false",
        entry.desiredOn ? "true" : "false",
        entry.actualOn ? "true" : "false",
        typeStr,
        blockReason,
        (unsigned long)entry.runningSecDay,
        (unsigned long)entry.runningSecMonth,
        (unsigned long)entry.runningSecYear,
        (unsigned long)entry.runningSecTotal,
        (double)entry.injectedMlDay,
        (double)entry.injectedMlMonth,
        (double)entry.injectedMlYear,
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
    const char* json = req.args ? req.args : req.json;
    if (!json) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_args\"}");
        return false;
    }

    const char* idStart = findJsonStringValueLocal(json, "id");
    idStart = skipWsLocal(idStart);
    if (!idStart) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_id\"}");
        return false;
    }
    const char* idEnd = strchr(idStart, '"');
    if (!idEnd) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"bad_id\"}");
        return false;
    }

    char id[24] = {0};
    size_t idLen = (size_t)(idEnd - idStart);
    if (idLen >= sizeof(id)) idLen = sizeof(id) - 1;
    memcpy(id, idStart, idLen);
    id[idLen] = '\0';

    uint8_t idx = 0;
    if (!findSlotById_(id, idx)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"unknown_device\"}");
        return false;
    }

    const char* valuePos = findJsonValueLocal(json, "value");
    valuePos = skipWsLocal(valuePos);
    if (!valuePos) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_value\"}");
        return false;
    }

    bool requested = false;
    if (strncmp(valuePos, "true", 4) == 0) {
        requested = true;
    } else if (strncmp(valuePos, "false", 5) == 0) {
        requested = false;
    } else {
        requested = (atoi(valuePos) != 0);
    }

    PoolDeviceSlot& s = slots_[idx];
    if (requested) {
        if (!s.def.enabled) {
            s.blockReason = POOL_DEVICE_BLOCK_DISABLED;
            snprintf(reply, replyLen, "{\"ok\":false,\"id\":\"%s\",\"err\":\"disabled\"}", id);
            return false;
        }
        if (!dependenciesSatisfied_(idx)) {
            s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            snprintf(reply, replyLen, "{\"ok\":false,\"id\":\"%s\",\"err\":\"interlock_blocked\"}", id);
            return false;
        }
    }

    s.desiredOn = requested;
    if (!requested) s.blockReason = POOL_DEVICE_BLOCK_NONE;

    bool ok = true;
    if (s.actualOn != requested) {
        ok = writeIo_(s.def.ioId, requested);
        if (ok) {
            s.actualOn = requested;
            s.blockReason = POOL_DEVICE_BLOCK_NONE;
        } else {
            s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
        }
    }

    tickDevices_(millis());
    snprintf(reply, replyLen, "{\"ok\":%s,\"id\":\"%s\"}", ok ? "true" : "false", id);
    return ok;
}

bool PoolDeviceModule::handlePoolRefill_(const CommandRequest& req, char* reply, size_t replyLen)
{
    const char* json = req.args ? req.args : req.json;
    if (!json) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_args\"}");
        return false;
    }

    const char* idStart = findJsonStringValueLocal(json, "id");
    idStart = skipWsLocal(idStart);
    if (!idStart) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"missing_id\"}");
        return false;
    }
    const char* idEnd = strchr(idStart, '"');
    if (!idEnd) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"bad_id\"}");
        return false;
    }

    char id[24] = {0};
    size_t idLen = (size_t)(idEnd - idStart);
    if (idLen >= sizeof(id)) idLen = sizeof(id) - 1;
    memcpy(id, idStart, idLen);
    id[idLen] = '\0';

    uint8_t idx = 0;
    if (!findSlotById_(id, idx)) {
        snprintf(reply, replyLen, "{\"ok\":false,\"err\":\"unknown_device\"}");
        return false;
    }

    PoolDeviceSlot& s = slots_[idx];
    float remaining = s.def.tankCapacityMl;
    const char* remPos = findJsonValueLocal(json, "remaining_ml");
    remPos = skipWsLocal(remPos);
    if (remPos) remaining = (float)atof(remPos);

    if (remaining < 0.0f) remaining = 0.0f;
    if (s.def.tankCapacityMl > 0.0f && remaining > s.def.tankCapacityMl) {
        remaining = s.def.tankCapacityMl;
    }
    s.tankRemainingMl = remaining;

    tickDevices_(millis());
    snprintf(reply, replyLen, "{\"ok\":true,\"id\":\"%s\",\"remaining_ml\":%.1f}", id, (double)remaining);
    return true;
}

bool PoolDeviceModule::configureRuntime_()
{
    if (runtimeReady_) return true;

    const uint32_t now = millis();
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

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

void PoolDeviceModule::updatePeriodResets_()
{
    if (!timeSvc_ || !timeSvc_->isSynced || !timeSvc_->epoch) return;
    if (!timeSvc_->isSynced(timeSvc_->ctx)) return;

    time_t now = (time_t)timeSvc_->epoch(timeSvc_->ctx);
    if (now <= 0) return;

    struct tm localNow{};
    if (!localtime_r(&now, &localNow)) return;

    if (!dateInitialized_) {
        lastYear_ = (int16_t)localNow.tm_year;
        lastMonth_ = (int8_t)localNow.tm_mon;
        lastYearDay_ = (int16_t)localNow.tm_yday;
        dateInitialized_ = true;
        return;
    }

    const bool yearChanged = (localNow.tm_year != lastYear_);
    const bool monthChanged = yearChanged || (localNow.tm_mon != lastMonth_);
    const bool dayChanged = monthChanged || (localNow.tm_yday != lastYearDay_);

    if (yearChanged || monthChanged || dayChanged) {
        for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
            PoolDeviceSlot& s = slots_[i];
            if (!s.used) continue;
            if (dayChanged) {
                s.runningMsDay = 0;
                s.injectedMlDay = 0.0f;
            }
            if (monthChanged) {
                s.runningMsMonth = 0;
                s.injectedMlMonth = 0.0f;
            }
            if (yearChanged) {
                s.runningMsYear = 0;
                s.injectedMlYear = 0.0f;
            }
        }
    }

    lastYear_ = (int16_t)localNow.tm_year;
    lastMonth_ = (int8_t)localNow.tm_mon;
    lastYearDay_ = (int16_t)localNow.tm_yday;
}

void PoolDeviceModule::tickDevices_(uint32_t nowMs)
{
    updatePeriodResets_();

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
            if (writeIo_(s.def.ioId, false)) {
                s.actualOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_INTERLOCK;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            changed = true;
        }

        if (s.desiredOn && !s.actualOn) {
            if (dependenciesSatisfied_(i)) {
                if (writeIo_(s.def.ioId, true)) {
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
            if (writeIo_(s.def.ioId, false)) {
                s.actualOn = false;
                s.blockReason = POOL_DEVICE_BLOCK_NONE;
            } else {
                s.blockReason = POOL_DEVICE_BLOCK_IO_ERROR;
            }
            changed = true;
        }

        if (s.actualOn && deltaMs > 0) {
            s.runningMsDay += deltaMs;
            s.runningMsMonth += deltaMs;
            s.runningMsYear += deltaMs;
            s.runningMsTotal += deltaMs;

            // Convert L/h to ml/ms for injected volume accumulation.
            const float flowPerMs = s.def.flowLPerHour / 3600.0f;
            const float injectedDelta = flowPerMs * (float)deltaMs;
            if (injectedDelta > 0.0f) {
                s.injectedMlDay += injectedDelta;
                s.injectedMlMonth += injectedDelta;
                s.injectedMlYear += injectedDelta;
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
            rt.runningSecMonth = toSeconds_(s.runningMsMonth);
            rt.runningSecYear = toSeconds_(s.runningMsYear);
            rt.runningSecTotal = toSeconds_(s.runningMsTotal);
            rt.injectedMlDay = s.injectedMlDay;
            rt.injectedMlMonth = s.injectedMlMonth;
            rt.injectedMlYear = s.injectedMlYear;
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
    if (!dataStore_) return false;
    if (slot.ioIdx >= IO_MAX_ENDPOINTS) return false;

    const IOEndpointRuntime& ep = dataStore_->data().io.endpoints[slot.ioIdx];
    if (!ep.valid) return false;
    if (ep.valueType != IO_VALUE_BOOL) return false;
    onOut = ep.boolValue;
    return true;
}

bool PoolDeviceModule::writeIo_(const char* ioId, bool on)
{
    if (!cmdSvc_ || !cmdSvc_->execute || !ioId || ioId[0] == '\0') return false;

    char args[96];
    char reply[128];
    snprintf(args, sizeof(args), "{\"id\":\"%s\",\"value\":%s}", ioId, on ? "true" : "false");
    return cmdSvc_->execute(cmdSvc_->ctx, "io.write", nullptr, args, reply, sizeof(reply));
}

bool PoolDeviceModule::findSlotById_(const char* id, uint8_t& idxOut) const
{
    if (!id || id[0] == '\0') return false;
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        const PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;
        if (strcmp(s.id, id) == 0) {
            idxOut = i;
            return true;
        }
    }
    return false;
}

bool PoolDeviceModule::parseDigitalIoId_(const char* ioId, uint8_t& ioIdxOut) const
{
    if (!ioId) return false;
    if (ioId[0] != 'd') return false;
    if (ioId[1] == '\0') return false;

    uint16_t v = 0;
    const char* p = ioId + 1;
    while (*p) {
        if (*p < '0' || *p > '9') return false;
        v = (uint16_t)((v * 10u) + (uint16_t)(*p - '0'));
        if (v >= IO_MAX_ENDPOINTS) return false;
        ++p;
    }

    ioIdxOut = (uint8_t)v;
    return true;
}

uint32_t PoolDeviceModule::toSeconds_(uint64_t ms)
{
    const uint64_t sec = ms / 1000ULL;
    return (sec > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)sec;
}

void PoolDeviceModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>("loghub");
    cmdSvc_ = services.get<CommandService>("cmd");
    timeSvc_ = services.get<TimeService>("time");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;

    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        PoolDeviceSlot& s = slots_[i];
        if (!s.used) continue;

        snprintf(cfgModuleName_[i], sizeof(cfgModuleName_[i]), "pdm/pd%u", (unsigned)i);
        snprintf(nvsEnabledKey_[i], sizeof(nvsEnabledKey_[i]), "pd%uen", (unsigned)i);
        snprintf(nvsTypeKey_[i], sizeof(nvsTypeKey_[i]), "pd%uty", (unsigned)i);
        snprintf(nvsDependsKey_[i], sizeof(nvsDependsKey_[i]), "pd%udp", (unsigned)i);
        snprintf(nvsFlowKey_[i], sizeof(nvsFlowKey_[i]), "pd%uflh", (unsigned)i);
        snprintf(nvsTankCapKey_[i], sizeof(nvsTankCapKey_[i]), "pd%utc", (unsigned)i);
        snprintf(nvsTankInitKey_[i], sizeof(nvsTankInitKey_[i]), "pd%uti", (unsigned)i);

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
