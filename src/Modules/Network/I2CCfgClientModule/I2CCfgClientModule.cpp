/**
 * @file I2CCfgClientModule.cpp
 * @brief Supervisor-side I2C cfg client implementation.
 */

#include "I2CCfgClientModule.h"
#include "Core/ErrorCodes.h"
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientRuntime.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::I2cCfgClientModule)
#include "Core/ModuleLog.h"

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint8_t kInterlinkBus = 1;  // Interlink is fixed on I2C controller 1 (Wire1 on ESP32).
constexpr uint8_t kI2cClientCfgProducerId = 51;
constexpr uint8_t kI2cClientCfgBranch = 2;
constexpr size_t kRuntimeStatusDomainBufSize = 640;
constexpr uint32_t kRuntimeCacheTtlMs = 5000U;
constexpr uint32_t kRemoteRetryCooldownMs = 3000U;
constexpr uint32_t kPriorityI2cHoldMs = 1500U;
static constexpr MqttConfigRouteProducer::Route kI2cClientCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::I2cCfg, kI2cClientCfgBranch}, "i2c/cfg/client", "i2c/cfg/client", (uint8_t)MqttPublishPriority::Normal, nullptr},
};

const char* opName(uint8_t op)
{
    switch (op) {
        case I2cCfgProtocol::OpPing: return "ping";
        case I2cCfgProtocol::OpListCount: return "list_count";
        case I2cCfgProtocol::OpListItem: return "list_item";
        case I2cCfgProtocol::OpListChildrenCount: return "list_children_count";
        case I2cCfgProtocol::OpListChildrenItem: return "list_children_item";
        case I2cCfgProtocol::OpGetModuleBegin: return "get_begin";
        case I2cCfgProtocol::OpGetModuleChunk: return "get_chunk";
        case I2cCfgProtocol::OpGetRuntimeStatusBegin: return "status_begin";
        case I2cCfgProtocol::OpGetRuntimeStatusChunk: return "status_chunk";
        case I2cCfgProtocol::OpGetRuntimeAlarmBegin: return "alarm_begin";
        case I2cCfgProtocol::OpGetRuntimeAlarmChunk: return "alarm_chunk";
        case I2cCfgProtocol::OpGetRuntimeUiValues: return "runtime_values";
        case I2cCfgProtocol::OpPatchBegin: return "patch_begin";
        case I2cCfgProtocol::OpPatchWrite: return "patch_write";
        case I2cCfgProtocol::OpPatchCommit: return "patch_commit";
        case I2cCfgProtocol::OpSystemAction: return "system_action";
        default: return "unknown";
    }
}

const char* statusName(uint8_t st)
{
    switch (st) {
        case I2cCfgProtocol::StatusOk: return "ok";
        case I2cCfgProtocol::StatusBadRequest: return "bad_request";
        case I2cCfgProtocol::StatusNotReady: return "not_ready";
        case I2cCfgProtocol::StatusRange: return "range";
        case I2cCfgProtocol::StatusOverflow: return "overflow";
        case I2cCfgProtocol::StatusFailed: return "failed";
        default: return "unknown";
    }
}

const char* statusDomainName(FlowStatusDomain domain)
{
    switch (domain) {
    case FlowStatusDomain::System: return "system";
    case FlowStatusDomain::Wifi: return "wifi";
    case FlowStatusDomain::Mqtt: return "mqtt";
    case FlowStatusDomain::I2c: return "i2c";
    case FlowStatusDomain::Pool: return "pool";
    case FlowStatusDomain::Alarm: return "alarm";
    default: return "unknown";
    }
}

bool appendTextChecked_(char* out, size_t outLen, size_t& pos, const char* text)
{
    if (!out || !text || outLen == 0 || pos >= outLen) return false;
    const size_t n = strlen(text);
    if (n >= (outLen - pos)) {
        out[outLen - 1] = '\0';
        return false;
    }
    memcpy(out + pos, text, n);
    pos += n;
    out[pos] = '\0';
    return true;
}

bool appendDomainFields_(char* out, size_t outLen, size_t& pos, const char* domainJson)
{
    static constexpr const char* kPrefix = "{\"ok\":true";
    if (!domainJson) return false;
    const size_t prefixLen = strlen(kPrefix);
    if (strncmp(domainJson, kPrefix, prefixLen) != 0) return false;

    const char* body = domainJson + prefixLen;
    if (*body == '}') return true;
    if (*body != ',') return false;
    ++body;

    const size_t bodyLen = strlen(body);
    if (bodyLen == 0 || body[bodyLen - 1] != '}') return false;

    if (!appendTextChecked_(out, outLen, pos, ",")) return false;
    if ((bodyLen - 1U) >= (outLen - pos)) {
        out[outLen - 1] = '\0';
        return false;
    }
    memcpy(out + pos, body, bodyLen - 1U);
    pos += (bodyLen - 1U);
    out[pos] = '\0';
    return true;
}

bool copyJsonReply_(char* out, size_t outLen, const uint8_t* resp, size_t respLen)
{
    if (!out || outLen == 0 || !resp || respLen == 0) return false;
    const size_t n = (respLen < (outLen - 1U)) ? respLen : (outLen - 1U);
    memcpy(out, resp, n);
    out[n] = '\0';
    return true;
}

bool writeApplyStatusError_(char* out, size_t outLen, const char* step, uint8_t status)
{
    const char* where = "flowcfg.apply";
    ErrorCode code = ErrorCode::Failed;

    if (strcmp(step, "begin") == 0) {
        where = "flowcfg.apply.begin";
        if (status == I2cCfgProtocol::StatusOverflow) {
            code = ErrorCode::ArgsTooLarge;
            where = "flowcfg.apply.begin.overflow";
        } else if (status == I2cCfgProtocol::StatusNotReady) {
            code = ErrorCode::NotReady;
            where = "flowcfg.apply.begin.not_ready";
        } else if (status == I2cCfgProtocol::StatusBadRequest) {
            code = ErrorCode::BadCfgJson;
            where = "flowcfg.apply.begin.bad_request";
        }
    } else if (strcmp(step, "write") == 0) {
        where = "flowcfg.apply.write";
        if (status == I2cCfgProtocol::StatusOverflow) {
            code = ErrorCode::ArgsTooLarge;
            where = "flowcfg.apply.write.overflow";
        } else if (status == I2cCfgProtocol::StatusRange) {
            where = "flowcfg.apply.write.range";
        } else if (status == I2cCfgProtocol::StatusNotReady) {
            code = ErrorCode::NotReady;
            where = "flowcfg.apply.write.not_ready";
        } else if (status == I2cCfgProtocol::StatusBadRequest) {
            code = ErrorCode::BadCfgJson;
            where = "flowcfg.apply.write.bad_request";
        }
    } else if (strcmp(step, "commit") == 0) {
        where = "flowcfg.apply.commit";
        if (status == I2cCfgProtocol::StatusNotReady) {
            code = ErrorCode::NotReady;
            where = "flowcfg.apply.commit.not_ready";
        } else if (status == I2cCfgProtocol::StatusBadRequest) {
            code = ErrorCode::BadCfgJson;
            where = "flowcfg.apply.commit.bad_request";
        } else if (status == I2cCfgProtocol::StatusFailed) {
            code = ErrorCode::CfgApplyFailed;
            where = "flowcfg.apply.commit.failed";
        }
    }

    return writeErrorJson(out, outLen, code, where);
}

}  // namespace

void I2CCfgClientModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::I2cCfg;
    constexpr uint8_t kCfgBranchId = kI2cClientCfgBranch;

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sdaVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sclVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(freqVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(addrVar_, kCfgModuleId, kCfgBranchId);

    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    if (!runtimeCacheMutex_) runtimeCacheMutex_ = xSemaphoreCreateMutex();
    if (!requestMutex_) requestMutex_ = xSemaphoreCreateMutex();
    if (!transportMutex_) transportMutex_ = xSemaphoreCreateMutex();
    if (!services.add(ServiceId::FlowCfg, &svc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::FlowCfg));
    }
    if (cmdSvc_ && cmdSvc_->registerHandler) {
        (void)cmdSvc_->registerHandler(cmdSvc_->ctx, "flow.system.reboot", &I2CCfgClientModule::cmdFlowReboot_, this);
        (void)cmdSvc_->registerHandler(cmdSvc_->ctx, "flow.system.factory_reset", &I2CCfgClientModule::cmdFlowFactoryReset_, this);
    }
    LOGI("I2C cfg client config/service registered");
    (void)logHub_;
}

void I2CCfgClientModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    cfgMqttPub_.configure(this,
                          kI2cClientCfgProducerId,
                          kI2cClientCfgRoutes,
                          (uint8_t)(sizeof(kI2cClientCfgRoutes) / sizeof(kI2cClientCfgRoutes[0])),
                          services);

    LOGI("onConfigLoaded enabled=%s bus=%u sda=%ld scl=%ld freq=%ld target=0x%02X",
         cfgData_.enabled ? "true" : "false",
         (unsigned)kInterlinkBus,
         (long)cfgData_.sda,
         (long)cfgData_.scl,
         (long)cfgData_.freqHz,
         (unsigned)cfgData_.targetAddr);
    startLink_();
}

void I2CCfgClientModule::loop()
{
    if (!cfgData_.enabled) return;
    if (priorityI2cWindowActive_()) return;

    const uint32_t now = millis();
    if ((int32_t)(now - nextRuntimeCacheRefreshAtMs_) < 0) return;
    nextRuntimeCacheRefreshAtMs_ = now + kRuntimeCacheTtlMs;

    (void)refreshRuntimeCacheIfNeeded_(true);
}

void I2CCfgClientModule::startLink_()
{
    LOGI("startLink requested enabled=%s ready=%s",
         cfgData_.enabled ? "true" : "false",
         ready_ ? "true" : "false");
    ready_ = false;
    reachable_ = false;
    retryAfterMs_ = 0;
    nextRuntimeCacheRefreshAtMs_ = 0;
    invalidateRuntimeCache_();
    if (!cfgData_.enabled) {
        LOGI("I2C cfg client disabled");
        return;
    }

    int32_t sda = cfgData_.sda;
    int32_t scl = cfgData_.scl;

    if (!link_.beginMaster(kInterlinkBus,
                           sda,
                           scl,
                           (uint32_t)(cfgData_.freqHz <= 0 ? 100000 : cfgData_.freqHz))) {
        LOGE("I2C cfg client start failed bus=%u sda=%ld scl=%ld freq=%ld target=0x%02X",
             (unsigned)kInterlinkBus,
             (long)sda,
             (long)scl,
             (long)cfgData_.freqHz,
             (unsigned)cfgData_.targetAddr);
        return;
    }
    ready_ = true;
    LOGI("I2C cfg client started app_role=client i2c_role=master target=0x%02X bus=%u sda=%ld scl=%ld freq=%ld",
         (unsigned)cfgData_.targetAddr,
         (unsigned)kInterlinkBus,
         (long)sda,
         (long)scl,
         (long)cfgData_.freqHz);

    uint8_t pingStatus = I2cCfgProtocol::StatusFailed;
    if (!pingFlow_(pingStatus)) {
        markRemoteUnavailable_();
        LOGW("I2C cfg ping transport failed target=0x%02X bus=%u sda=%ld scl=%ld freq=%ld (check wiring/power/address)",
             (unsigned)cfgData_.targetAddr,
             (unsigned)kInterlinkBus,
             (long)cfgData_.sda,
             (long)cfgData_.scl,
             (long)cfgData_.freqHz);
    } else if (pingStatus != I2cCfgProtocol::StatusOk) {
        markRemoteUnavailable_();
        LOGW("I2C cfg ping returned status=%u (%s) target=0x%02X",
             (unsigned)pingStatus,
             statusName(pingStatus),
             (unsigned)cfgData_.targetAddr);
    } else {
        markRemoteAvailable_();
        LOGI("I2C cfg ping ok target=0x%02X", (unsigned)cfgData_.targetAddr);
        (void)refreshRuntimeCacheIfNeeded_(true);
    }
}

bool I2CCfgClientModule::ensureReady_()
{
    if (!ready_) {
        if (!cfgData_.enabled) {
            LOGW("ensureReady failed: module disabled");
            return false;
        }
        LOGW("ensureReady: link not ready, attempting restart");
        startLink_();
        return ready_ && reachable_;
    }
    if (reachable_) return true;

    const uint32_t now = millis();
    if ((int32_t)(now - retryAfterMs_) < 0) {
        return false;
    }

    uint8_t pingStatus = I2cCfgProtocol::StatusFailed;
    if (!pingFlow_(pingStatus) || pingStatus != I2cCfgProtocol::StatusOk) {
        markRemoteUnavailable_();
        return false;
    }
    markRemoteAvailable_();
    LOGI("I2C cfg remote reachable again target=0x%02X", (unsigned)cfgData_.targetAddr);
    return true;
}

bool I2CCfgClientModule::isReady_() const
{
    return ready_ && (reachable_ || runtimeCacheValid_);
}

void I2CCfgClientModule::recoverLinkAfterApplyFailure_(const char* step, bool transportOk, uint8_t status)
{
    LOGW("Recovering I2C cfg link after apply failure step=%s transport=%s status=%u (%s)",
         step ? step : "unknown",
         transportOk ? "ok" : "failed",
         (unsigned)status,
         statusName(status));
    delay(2);
    startLink_();
}

void I2CCfgClientModule::markRemoteUnavailable_()
{
    reachable_ = false;
    retryAfterMs_ = millis() + kRemoteRetryCooldownMs;
    publishFlowRemoteReady_(false);
}

void I2CCfgClientModule::markRemoteAvailable_()
{
    reachable_ = true;
    retryAfterMs_ = 0;
}

bool I2CCfgClientModule::beginRequestSession_(TickType_t timeoutTicks, bool interactive)
{
    if (interactive) notePriorityI2cRequest_(kPriorityI2cHoldMs);
    if (!requestMutex_) return false;
    if (xSemaphoreTake(requestMutex_, timeoutTicks) != pdTRUE) {
        return false;
    }
    if (interactive) notePriorityI2cRequest_(kPriorityI2cHoldMs);
    return true;
}

void I2CCfgClientModule::endRequestSession_()
{
    if (requestMutex_) xSemaphoreGive(requestMutex_);
}

void I2CCfgClientModule::notePriorityI2cRequest_(uint32_t holdMs)
{
    const uint32_t now = millis();
    const uint32_t guard = (holdMs == 0U) ? kPriorityI2cHoldMs : holdMs;
    priorityI2cBusyUntilMs_ = now + guard;
}

bool I2CCfgClientModule::priorityI2cWindowActive_() const
{
    return (int32_t)(millis() - priorityI2cBusyUntilMs_) < 0;
}

uint8_t I2CCfgClientModule::runtimeStatusDomainCacheIndex_(FlowStatusDomain domain)
{
    const uint8_t raw = (uint8_t)domain;
    return (raw > 0U) ? (uint8_t)(raw - 1U) : 0U;
}

void I2CCfgClientModule::invalidateRuntimeCache_()
{
    if (!runtimeCacheMutex_) {
        runtimeCacheValid_ = false;
        runtimeCacheFetchedAtMs_ = 0U;
        memset(runtimeStatusDomainCache_, 0, sizeof(runtimeStatusDomainCache_));
        memset(runtimeStatusDomainCacheValid_, 0, sizeof(runtimeStatusDomainCacheValid_));
        publishFlowRemoteReady_(false);
        return;
    }

    if (xSemaphoreTake(runtimeCacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return;
    runtimeCacheValid_ = false;
    runtimeCacheFetchedAtMs_ = 0U;
    memset(runtimeStatusDomainCache_, 0, sizeof(runtimeStatusDomainCache_));
    memset(runtimeStatusDomainCacheValid_, 0, sizeof(runtimeStatusDomainCacheValid_));
    xSemaphoreGive(runtimeCacheMutex_);
    publishFlowRemoteReady_(false);
}

bool I2CCfgClientModule::refreshRuntimeCacheIfNeeded_(bool force)
{
    if (!runtimeCacheMutex_) return false;

    const uint32_t now = millis();
    if (!force) {
        if (xSemaphoreTake(runtimeCacheMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
            const bool fresh = runtimeCacheValid_ && ((uint32_t)(now - runtimeCacheFetchedAtMs_) < kRuntimeCacheTtlMs);
            xSemaphoreGive(runtimeCacheMutex_);
            if (fresh) return true;
        }
    }

    if (!ensureReady_()) {
        if (xSemaphoreTake(runtimeCacheMutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
            const bool hasCache = runtimeCacheValid_;
            xSemaphoreGive(runtimeCacheMutex_);
            return hasCache;
        }
        return false;
    }

    if (xSemaphoreTake(runtimeCacheMutex_, pdMS_TO_TICKS(500)) != pdTRUE) return false;

    const uint32_t lockedNow = millis();
    if (!force) {
        const bool fresh = runtimeCacheValid_ && ((uint32_t)(lockedNow - runtimeCacheFetchedAtMs_) < kRuntimeCacheTtlMs);
        if (fresh) {
            xSemaphoreGive(runtimeCacheMutex_);
            return true;
        }
    }

    const bool hadCache = runtimeCacheValid_;
    bool ok = true;
    memset(runtimeStatusDomainFetchScratch_, 0, sizeof(runtimeStatusDomainFetchScratch_));
    memset(runtimeStatusDomainCacheNext_, 0, sizeof(runtimeStatusDomainCacheNext_));
    memset(runtimeStatusDomainCacheValidNext_, 0, sizeof(runtimeStatusDomainCacheValidNext_));
    static constexpr FlowStatusDomain kDomains[] = {
        FlowStatusDomain::System,
        FlowStatusDomain::Wifi,
        FlowStatusDomain::Mqtt,
        FlowStatusDomain::I2c,
        FlowStatusDomain::Pool,
    };

    for (size_t i = 0; i < (sizeof(kDomains) / sizeof(kDomains[0])); ++i) {
        if (priorityI2cWindowActive_()) {
            xSemaphoreGive(runtimeCacheMutex_);
            return hadCache;
        }
        memset(runtimeStatusDomainFetchScratch_, 0, sizeof(runtimeStatusDomainFetchScratch_));
        if (!fetchRuntimeStatusDomainUncached_(kDomains[i],
                                               runtimeStatusDomainFetchScratch_,
                                               sizeof(runtimeStatusDomainFetchScratch_))) {
            ok = false;
            break;
        }
        const uint8_t idx = runtimeStatusDomainCacheIndex_(kDomains[i]);
        snprintf(runtimeStatusDomainCacheNext_[idx],
                 sizeof(runtimeStatusDomainCacheNext_[idx]),
                 "%s",
                 runtimeStatusDomainFetchScratch_);
        runtimeStatusDomainCacheValidNext_[idx] = true;
    }

    if (ok) {
        memcpy(runtimeStatusDomainCache_, runtimeStatusDomainCacheNext_, sizeof(runtimeStatusDomainCache_));
        memcpy(runtimeStatusDomainCacheValid_, runtimeStatusDomainCacheValidNext_, sizeof(runtimeStatusDomainCacheValid_));
    }

    if (ok) {
        runtimeCacheValid_ = true;
        runtimeCacheFetchedAtMs_ = millis();
    }

    xSemaphoreGive(runtimeCacheMutex_);
    if (ok) {
        publishFlowRemoteSnapshotFromCache_();
    }
    return ok || hadCache;
}

void I2CCfgClientModule::publishFlowRemoteReady_(bool ready)
{
    if (!dsSvc_ || !dsSvc_->store) return;
    DataStore& ds = *dsSvc_->store;
    (void)setFlowRemoteReady(ds, ready);
    if (!ready) {
        (void)setFlowRemoteLinkOk(ds, false);
    }
}

bool I2CCfgClientModule::parseFlowRemoteSnapshotFromCache_(FlowRemoteRuntimeData& out)
{
    if (!runtimeCacheMutex_) return false;
    if (xSemaphoreTake(runtimeCacheMutex_, pdMS_TO_TICKS(200)) != pdTRUE) return false;

    FlowRemoteRuntimeData snapshot{};
    snapshot.ready = runtimeCacheValid_;
    StaticJsonDocument<640> doc;

    auto parseDomain = [&](FlowStatusDomain domain) -> JsonObjectConst {
        const uint8_t idx = runtimeStatusDomainCacheIndex_(domain);
        const bool valid = idx < (sizeof(runtimeStatusDomainCacheValid_) / sizeof(runtimeStatusDomainCacheValid_[0])) &&
                           runtimeStatusDomainCacheValid_[idx] &&
                           runtimeStatusDomainCache_[idx][0] != '\0';
        if (!valid) return JsonObjectConst();
        doc.clear();
        const DeserializationError err = deserializeJson(doc, runtimeStatusDomainCache_[idx]);
        if (err || !doc.is<JsonObjectConst>()) {
            doc.clear();
            return JsonObjectConst();
        }
        JsonObjectConst root = doc.as<JsonObjectConst>();
        if (!(root["ok"] | false)) {
            doc.clear();
            return JsonObjectConst();
        }
        return root;
    };

    JsonObjectConst root = parseDomain(FlowStatusDomain::System);
    if (root.isNull()) {
        xSemaphoreGive(runtimeCacheMutex_);
        return false;
    }
    snprintf(snapshot.firmware, sizeof(snapshot.firmware), "%s", root["fw"] | "");
    {
        JsonObjectConst flowHeap = root["heap"];
        if (!flowHeap.isNull()) {
            snapshot.hasHeapFrag = true;
            snapshot.heapFragPct = (uint8_t)(flowHeap["frag"] | 0U);
        }
    }

    root = parseDomain(FlowStatusDomain::Wifi);
    if (root.isNull()) {
        xSemaphoreGive(runtimeCacheMutex_);
        return false;
    }
    {
        JsonObjectConst flowWifi = root["wifi"];
        snapshot.hasRssi = flowWifi["hrss"] | false;
        snapshot.rssiDbm = (int32_t)(flowWifi["rssi"] | -127);
    }

    root = parseDomain(FlowStatusDomain::Mqtt);
    if (root.isNull()) {
        xSemaphoreGive(runtimeCacheMutex_);
        return false;
    }
    {
        JsonObjectConst flowMqtt = root["mqtt"];
        snapshot.mqttReady = flowMqtt["rdy"] | false;
        snapshot.mqttRxDrop = (uint32_t)(flowMqtt["rxdrp"] | 0U);
        snapshot.mqttParseFail = (uint32_t)(flowMqtt["prsf"] | 0U);
    }

    root = parseDomain(FlowStatusDomain::I2c);
    if (root.isNull()) {
        xSemaphoreGive(runtimeCacheMutex_);
        return false;
    }
    {
        JsonObjectConst i2c = root["i2c"];
        snapshot.linkOk = i2c["lnk"] | false;
        snapshot.i2cReqCount = (uint32_t)(i2c["req"] | 0U);
        snapshot.i2cBadReqCount = (uint32_t)(i2c["breq"] | 0U);
        snapshot.i2cLastReqAgoMs = (uint32_t)(i2c["ago"] | 0U);
    }

    root = parseDomain(FlowStatusDomain::Pool);
    if (root.isNull()) {
        xSemaphoreGive(runtimeCacheMutex_);
        return false;
    }
    {
        JsonObjectConst pool = root["pool"];
        if (!pool.isNull()) {
            snapshot.hasPoolModes = pool["has"] | false;
            snapshot.filtrationAuto = pool["auto"] | false;
            snapshot.winterMode = pool["wint"] | false;
            snapshot.phAutoMode = pool["pha"] | false;
            snapshot.orpAutoMode = pool["ora"] | false;
            snapshot.filtrationOn = pool["fil"] | false;
            snapshot.phPumpOn = pool["php"] | false;
            snapshot.chlorinePumpOn = pool["clp"] | false;

            JsonVariantConst phVar = pool["ph"];
            if (!phVar.isNull()) {
                snapshot.hasPh = true;
                snapshot.phValue = phVar.as<float>();
            }
            JsonVariantConst orpVar = pool["orp"];
            if (!orpVar.isNull()) {
                snapshot.hasOrp = true;
                snapshot.orpValue = orpVar.as<float>();
            }
            JsonVariantConst waterTempVar = pool["wat"];
            if (!waterTempVar.isNull()) {
                snapshot.hasWaterTemp = true;
                snapshot.waterTemp = waterTempVar.as<float>();
            }
            JsonVariantConst airTempVar = pool["air"];
            if (!airTempVar.isNull()) {
                snapshot.hasAirTemp = true;
                snapshot.airTemp = airTempVar.as<float>();
            }
        }
    }

    xSemaphoreGive(runtimeCacheMutex_);
    out = snapshot;
    return true;
}

void I2CCfgClientModule::publishFlowRemoteSnapshotFromCache_()
{
    if (!dsSvc_ || !dsSvc_->store) return;
    FlowRemoteRuntimeData snapshot{};
    if (!parseFlowRemoteSnapshotFromCache_(snapshot)) return;
    applyFlowRemoteRuntimeSnapshot(*dsSvc_->store, snapshot);
}

bool I2CCfgClientModule::pingFlow_(uint8_t& statusOut)
{
    uint8_t resp[8] = {0};
    size_t respLen = 0;
    const bool ok = transact_(I2cCfgProtocol::OpPing, nullptr, 0, statusOut, resp, sizeof(resp), respLen);
    if (ok && statusOut == I2cCfgProtocol::StatusOk) {
        const unsigned protoVer = (respLen > 0) ? (unsigned)resp[0] : 0U;
        const unsigned echoAddr = (respLen > 1) ? (unsigned)resp[1] : 0U;
        LOGI("I2C cfg ping reply ver=%u addr=0x%02X len=%u", protoVer, echoAddr, (unsigned)respLen);
    }
    return ok;
}

bool I2CCfgClientModule::transact_(uint8_t op,
                                   const uint8_t* reqPayload,
                                   size_t reqLen,
                                   uint8_t& statusOut,
                                   uint8_t* respPayload,
                                   size_t respPayloadMax,
                                   size_t& respLenOut)
{
    if (!transportMutex_) return false;
    if (xSemaphoreTake(transportMutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGW("I2C transact timeout waiting bus lock op=%s", opName(op));
        return false;
    }
    const bool ok = transactUnlocked_(op, reqPayload, reqLen, statusOut, respPayload, respPayloadMax, respLenOut);
    xSemaphoreGive(transportMutex_);
    return ok;
}

bool I2CCfgClientModule::transactUnlocked_(uint8_t op,
                                           const uint8_t* reqPayload,
                                           size_t reqLen,
                                           uint8_t& statusOut,
                                           uint8_t* respPayload,
                                           size_t respPayloadMax,
                                           size_t& respLenOut)
{
    statusOut = I2cCfgProtocol::StatusFailed;
    respLenOut = 0;
    if (!ready_) {
        LOGW("I2C transact aborted (not ready) op=%s", opName(op));
        return false;
    }
    if (reqLen > I2cCfgProtocol::MaxPayload) {
        LOGE("I2C transact payload too large op=%s req_len=%u max=%u",
             opName(op),
             (unsigned)reqLen,
             (unsigned)I2cCfgProtocol::MaxPayload);
        return false;
    }

    uint8_t tx[I2cCfgProtocol::MaxReqFrame] = {0};
    const uint8_t seq = seq_++;
    tx[0] = I2cCfgProtocol::ReqMagic;
    tx[1] = I2cCfgProtocol::Version;
    tx[2] = op;
    tx[3] = seq;
    tx[4] = (uint8_t)reqLen;
    if (reqPayload && reqLen > 0) memcpy(tx + I2cCfgProtocol::ReqHeaderSize, reqPayload, reqLen);

    constexpr uint8_t kMaxAttempts = 3;
    uint8_t rx[128] = {0};
    size_t rxLen = 0;
    for (uint8_t attempt = 0; attempt < kMaxAttempts; ++attempt) {
        memset(rx, 0, sizeof(rx));
        rxLen = 0;
        if (!link_.transfer(cfgData_.targetAddr, tx, I2cCfgProtocol::ReqHeaderSize + reqLen, rx, sizeof(rx), rxLen)) {
            if (attempt + 1 < kMaxAttempts) {
                delay(2);
                continue;
            }
            markRemoteUnavailable_();
            LOGW("I2C transfer failed op=%s seq=%u addr=0x%02X req=%u",
                 opName(op),
                 (unsigned)seq,
                 (unsigned)cfgData_.targetAddr,
                 (unsigned)(I2cCfgProtocol::ReqHeaderSize + reqLen));
            LOGW("I2C link cfg bus=%u sda=%ld scl=%ld freq=%ld",
                 (unsigned)kInterlinkBus,
                 (long)cfgData_.sda,
                 (long)cfgData_.scl,
                 (long)cfgData_.freqHz);
            return false;
        }
        if (rxLen < I2cCfgProtocol::RespHeaderSize) {
            if (attempt + 1 < kMaxAttempts) {
                delay(2);
                continue;
            }
            markRemoteUnavailable_();
            LOGW("I2C short response op=%s seq=%u len=%u",
                 opName(op),
                 (unsigned)seq,
                 (unsigned)rxLen);
            return false;
        }
        if (rx[0] != I2cCfgProtocol::RespMagic || rx[1] != I2cCfgProtocol::Version) {
            if (attempt + 1 < kMaxAttempts) {
                delay(2);
                continue;
            }
            markRemoteUnavailable_();
            LOGW("I2C bad header op=%s seq=%u magic=0x%02X ver=%u",
                 opName(op),
                 (unsigned)seq,
                 (unsigned)rx[0],
                 (unsigned)rx[1]);
            return false;
        }
        if (rx[2] != op || rx[3] != seq) {
            if (attempt + 1 < kMaxAttempts) {
                delay(2);
                continue;
            }
            markRemoteUnavailable_();
            LOGW("I2C op/seq mismatch expected_op=%u expected_seq=%u got_op=%u got_seq=%u",
                 (unsigned)op,
                 (unsigned)seq,
                 (unsigned)rx[2],
                 (unsigned)rx[3]);
            return false;
        }

        break;
    }

    markRemoteAvailable_();

    statusOut = rx[4];
    const size_t payloadLen = rx[5];
    if (payloadLen > I2cCfgProtocol::MaxPayload) {
        markRemoteUnavailable_();
        LOGW("I2C invalid payload len op=%s seq=%u payload=%u",
             opName(op),
             (unsigned)seq,
             (unsigned)payloadLen);
        return false;
    }
    if (rxLen < (I2cCfgProtocol::RespHeaderSize + payloadLen)) {
        markRemoteUnavailable_();
        LOGW("I2C truncated response op=%s seq=%u rx_len=%u expected=%u",
             opName(op),
             (unsigned)seq,
             (unsigned)rxLen,
             (unsigned)(I2cCfgProtocol::RespHeaderSize + payloadLen));
        return false;
    }

    if (respPayload && respPayloadMax > 0 && payloadLen > 0) {
        const size_t n = (payloadLen < respPayloadMax) ? payloadLen : respPayloadMax;
        memcpy(respPayload, rx + I2cCfgProtocol::RespHeaderSize, n);
        respLenOut = n;
    } else {
        respLenOut = 0;
    }
    if (statusOut != I2cCfgProtocol::StatusOk) {
        LOGW("I2C response status op=%s seq=%u status=%u (%s) payload_len=%u",
             opName(op),
             (unsigned)seq,
             (unsigned)statusOut,
             statusName(statusOut),
             (unsigned)payloadLen);
    }
    return true;
}

bool I2CCfgClientModule::listModulesJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    if (!beginRequestSession_(pdMS_TO_TICKS(1500), true)) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.modules.busy");
        return false;
    }
    if (!ensureReady_()) {
        LOGW("listModules aborted: link not ready");
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.modules");
        endRequestSession_();
        return false;
    }

    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;
    const bool okCount = transact_(I2cCfgProtocol::OpListCount, nullptr, 0, status, resp, sizeof(resp), respLen);
    if (!okCount ||
        status != I2cCfgProtocol::StatusOk ||
        respLen < 1) {
        LOGW("listModules failed step=count transport=%s status=%u (%s) resp_len=%u",
             okCount ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen);
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.modules.count");
        endRequestSession_();
        return false;
    }
    const uint8_t count = resp[0];
    LOGI("flowcfg.list begin count=%u", (unsigned)count);

    size_t pos = 0;
    int wrote = snprintf(out + pos, outLen - pos, "{\"ok\":true,\"modules\":[");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
        endRequestSession_();
        return false;
    }
    pos += (size_t)wrote;

    for (uint8_t i = 0; i < count; ++i) {
        const uint8_t req[1] = {i};
        memset(resp, 0, sizeof(resp));
        respLen = 0;
        const bool okItem = transact_(I2cCfgProtocol::OpListItem, req, sizeof(req), status, resp, sizeof(resp), respLen);
        if (!okItem ||
            status != I2cCfgProtocol::StatusOk ||
            respLen == 0) {
            LOGW("listModules failed step=item idx=%u transport=%s status=%u (%s) resp_len=%u",
                 (unsigned)i,
                 okItem ? "ok" : "failed",
                 (unsigned)status,
                 statusName(status),
                 (unsigned)respLen);
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.modules.item");
            endRequestSession_();
            return false;
        }

        char moduleName[64] = {0};
        const size_t n = (respLen < (sizeof(moduleName) - 1)) ? respLen : (sizeof(moduleName) - 1);
        memcpy(moduleName, resp, n);
        moduleName[n] = '\0';

        wrote = snprintf(out + pos, outLen - pos, "%s\"%s\"", (i == 0) ? "" : ",", moduleName);
        if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
            (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.modules.json");
            endRequestSession_();
            return false;
        }
        pos += (size_t)wrote;
    }

    wrote = snprintf(out + pos, outLen - pos, "]}");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
        endRequestSession_();
        return false;
    }
    LOGI("flowcfg.list done count=%u", (unsigned)count);
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::listChildrenJson_(const char* prefix, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    if (!beginRequestSession_(pdMS_TO_TICKS(1500), true)) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.children.busy");
        return false;
    }
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.children");
        endRequestSession_();
        return false;
    }

    char prefixNorm[64] = {0};
    if (prefix && prefix[0] != '\0') {
        snprintf(prefixNorm, sizeof(prefixNorm), "%s", prefix);
    }
    size_t prefixLen = strnlen(prefixNorm, sizeof(prefixNorm));
    while (prefixLen > 0 && prefixNorm[0] == '/') {
        memmove(prefixNorm, prefixNorm + 1, prefixLen);
        --prefixLen;
    }
    while (prefixLen > 0 && prefixNorm[prefixLen - 1] == '/') {
        prefixNorm[prefixLen - 1] = '\0';
        --prefixLen;
    }
    if (prefixLen >= (I2cCfgProtocol::MaxPayload - 1)) {
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.children.prefix");
        endRequestSession_();
        return false;
    }

    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;
    const bool okCount = transact_(I2cCfgProtocol::OpListChildrenCount,
                                   prefixLen > 0 ? (const uint8_t*)prefixNorm : nullptr,
                                   prefixLen,
                                   status,
                                   resp,
                                   sizeof(resp),
                                   respLen);
    if (!okCount ||
        status != I2cCfgProtocol::StatusOk ||
        respLen < 1) {
        LOGW("listChildren failed step=count prefix=%s transport=%s status=%u (%s) resp_len=%u",
             prefixLen > 0 ? prefixNorm : "<root>",
             okCount ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen);
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.children.count");
        endRequestSession_();
        return false;
    }

    const uint8_t count = resp[0];
    const bool hasExact = (respLen >= 2 && resp[1] != 0);
    LOGI("flowcfg.children begin prefix=%s count=%u has_exact=%s",
         prefixLen > 0 ? prefixNorm : "<root>",
         (unsigned)count,
         hasExact ? "true" : "false");

    size_t pos = 0;
    int wrote = snprintf(out + pos,
                         outLen - pos,
                         "{\"ok\":true,\"prefix\":\"%s\",\"has_exact\":%s,\"children\":[",
                         prefixNorm,
                         hasExact ? "true" : "false");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
        endRequestSession_();
        return false;
    }
    pos += (size_t)wrote;

    for (uint8_t i = 0; i < count; ++i) {
        uint8_t req[I2cCfgProtocol::MaxPayload] = {0};
        req[0] = i;
        if (prefixLen > 0) memcpy(req + 1, prefixNorm, prefixLen);

        memset(resp, 0, sizeof(resp));
        respLen = 0;
        const bool okItem = transact_(I2cCfgProtocol::OpListChildrenItem,
                                      req,
                                      prefixLen + 1,
                                      status,
                                      resp,
                                      sizeof(resp),
                                      respLen);
        if (!okItem ||
            status != I2cCfgProtocol::StatusOk ||
            respLen == 0) {
            LOGW("listChildren failed step=item idx=%u prefix=%s transport=%s status=%u (%s) resp_len=%u",
                 (unsigned)i,
                 prefixLen > 0 ? prefixNorm : "<root>",
                 okItem ? "ok" : "failed",
                (unsigned)status,
                 statusName(status),
                 (unsigned)respLen);
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.children.item");
            endRequestSession_();
            return false;
        }

        char childName[40] = {0};
        const size_t n = (respLen < (sizeof(childName) - 1)) ? respLen : (sizeof(childName) - 1);
        memcpy(childName, resp, n);
        childName[n] = '\0';
        LOGD("flowcfg.children item[%u]=%s", (unsigned)i, childName);

        wrote = snprintf(out + pos, outLen - pos, "%s\"%s\"", (i == 0) ? "" : ",", childName);
        if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
            (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.children.json");
            endRequestSession_();
            return false;
        }
        pos += (size_t)wrote;
    }

    wrote = snprintf(out + pos, outLen - pos, "]}");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
        endRequestSession_();
        return false;
    }
    LOGI("flowcfg.children done prefix=%s count=%u",
         prefixLen > 0 ? prefixNorm : "<root>",
         (unsigned)count);
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::getModuleJson_(const char* module, char* out, size_t outLen, bool* truncated)
{
    if (truncated) *truncated = false;
    if (!out || outLen == 0 || !module || module[0] == '\0') return false;
    if (!beginRequestSession_(pdMS_TO_TICKS(1500), true)) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.get.busy");
        return false;
    }
    if (!ensureReady_()) {
        endRequestSession_();
        return false;
    }

    const size_t moduleLen = strnlen(module, I2cCfgProtocol::MaxPayload);
    if (moduleLen == 0 || moduleLen >= I2cCfgProtocol::MaxPayload) {
        endRequestSession_();
        return false;
    }
    LOGI("flowcfg.get begin module=%s", module);

    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;
    const bool okBegin = transact_(I2cCfgProtocol::OpGetModuleBegin,
                   (const uint8_t*)module,
                   moduleLen,
                   status,
                   resp,
                   sizeof(resp),
                   respLen);
    if (!okBegin ||
        status != I2cCfgProtocol::StatusOk ||
        respLen < 3) {
        LOGW("getModule failed module=%s step=begin transport=%s status=%u (%s) resp_len=%u",
             module,
             okBegin ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen);
        endRequestSession_();
        return false;
    }

    const size_t totalLen = (size_t)resp[0] | ((size_t)resp[1] << 8);
    const bool isTruncated = (resp[2] & 0x02u) != 0;
    if (truncated) *truncated = isTruncated;
    LOGI("flowcfg.get info module=%s total=%u truncated=%s",
         module,
         (unsigned)totalLen,
         isTruncated ? "true" : "false");

    if (totalLen + 1 > outLen) {
        endRequestSession_();
        return false;
    }

    size_t written = 0;
    uint16_t chunkCount = 0;
    while (written < totalLen) {
        const size_t remain = totalLen - written;
        const uint8_t want = (uint8_t)((remain > I2cCfgProtocol::MaxPayload) ? I2cCfgProtocol::MaxPayload : remain);
        const uint8_t req[3] = {
            (uint8_t)(written & 0xFFu),
            (uint8_t)((written >> 8) & 0xFFu),
            want
        };
        memset(resp, 0, sizeof(resp));
        respLen = 0;
        const bool okChunk = transact_(I2cCfgProtocol::OpGetModuleChunk, req, sizeof(req), status, resp, sizeof(resp), respLen);
        if (!okChunk ||
            status != I2cCfgProtocol::StatusOk) {
            LOGW("getModule failed module=%s step=chunk off=%u want=%u transport=%s status=%u (%s) resp_len=%u",
                 module,
                 (unsigned)written,
                 (unsigned)want,
                 okChunk ? "ok" : "failed",
                 (unsigned)status,
                 statusName(status),
                 (unsigned)respLen);
            endRequestSession_();
            return false;
        }
        if (respLen == 0 || (written + respLen) > totalLen) {
            LOGW("getModule invalid chunk module=%s off=%u resp_len=%u total=%u",
                 module,
                 (unsigned)written,
                 (unsigned)respLen,
                 (unsigned)totalLen);
            endRequestSession_();
            return false;
        }
        LOGD("flowcfg.get chunk module=%s off=%u got=%u remain=%u",
             module,
             (unsigned)written,
             (unsigned)respLen,
             (unsigned)(totalLen - (written + respLen)));
        memcpy(out + written, resp, respLen);
        written += respLen;
        ++chunkCount;
    }
    out[written] = '\0';
    LOGI("flowcfg.get done module=%s bytes=%u chunks=%u",
         module,
         (unsigned)written,
         (unsigned)chunkCount);
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::applyPatchJson_(const char* patch, char* out, size_t outLen)
{
    if (!out || outLen == 0 || !patch) return false;
    if (!beginRequestSession_(pdMS_TO_TICKS(2000), true)) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.apply.busy");
        return false;
    }
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.apply");
        endRequestSession_();
        return false;
    }

    const size_t len = strnlen(patch, Limits::JsonConfigApplyBuf + 1);
    if (len == 0 || len > Limits::JsonConfigApplyBuf) {
        (void)writeErrorJson(out, outLen, ErrorCode::ArgsTooLarge, "flowcfg.apply.size");
        endRequestSession_();
        return false;
    }

    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;

    const uint8_t beginReq[2] = {(uint8_t)(len & 0xFFu), (uint8_t)((len >> 8) & 0xFFu)};
    const bool okPatchBegin = transact_(I2cCfgProtocol::OpPatchBegin, beginReq, sizeof(beginReq), status, resp, sizeof(resp), respLen);
    if (!okPatchBegin ||
        status != I2cCfgProtocol::StatusOk) {
        LOGW("applyPatch failed step=begin transport=%s status=%u (%s) resp_len=%u len=%u",
             okPatchBegin ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen,
             (unsigned)len);
        if (!okPatchBegin) {
            (void)writeErrorJson(out, outLen, ErrorCode::IoError, "flowcfg.apply.begin.transport");
        } else {
            (void)writeApplyStatusError_(out, outLen, "begin", status);
        }
        recoverLinkAfterApplyFailure_("begin", okPatchBegin, status);
        endRequestSession_();
        return false;
    }

    size_t offset = 0;
    while (offset < len) {
        const size_t remain = len - offset;
        const size_t chunk = (remain > (I2cCfgProtocol::MaxPayload - 2)) ? (I2cCfgProtocol::MaxPayload - 2) : remain;
        uint8_t req[I2cCfgProtocol::MaxPayload] = {0};
        req[0] = (uint8_t)(offset & 0xFFu);
        req[1] = (uint8_t)((offset >> 8) & 0xFFu);
        memcpy(req + 2, patch + offset, chunk);

        const bool okPatchWrite = transact_(I2cCfgProtocol::OpPatchWrite, req, chunk + 2, status, resp, sizeof(resp), respLen);
        if (!okPatchWrite ||
            status != I2cCfgProtocol::StatusOk) {
            LOGW("applyPatch failed step=write off=%u chunk=%u transport=%s status=%u (%s) resp_len=%u",
                 (unsigned)offset,
                 (unsigned)chunk,
                 okPatchWrite ? "ok" : "failed",
                 (unsigned)status,
                 statusName(status),
                 (unsigned)respLen);
            if (!okPatchWrite) {
                (void)writeErrorJson(out, outLen, ErrorCode::IoError, "flowcfg.apply.write.transport");
            } else {
                (void)writeApplyStatusError_(out, outLen, "write", status);
            }
            recoverLinkAfterApplyFailure_("write", okPatchWrite, status);
            endRequestSession_();
            return false;
        }
        offset += chunk;
    }

    const bool okPatchCommit = transact_(I2cCfgProtocol::OpPatchCommit, nullptr, 0, status, resp, sizeof(resp), respLen);
    if (!okPatchCommit ||
        status != I2cCfgProtocol::StatusOk) {
        LOGW("applyPatch failed step=commit transport=%s status=%u (%s) resp_len=%u",
             okPatchCommit ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen);
        if (!okPatchCommit) {
            (void)writeErrorJson(out, outLen, ErrorCode::IoError, "flowcfg.apply.commit.transport");
        } else if (respLen > 0 && copyJsonReply_(out, outLen, resp, respLen)) {
            // Preserve the remote Flow.IO error JSON so the web UI can explain the refusal.
        } else {
            (void)writeApplyStatusError_(out, outLen, "commit", status);
        }
        recoverLinkAfterApplyFailure_("commit", okPatchCommit, status);
        endRequestSession_();
        return false;
    }

    if (respLen == 0) {
        (void)writeOkJson(out, outLen, "flowcfg.apply");
        invalidateRuntimeCache_();
        endRequestSession_();
        return true;
    }

    const size_t n = (respLen < (outLen - 1)) ? respLen : (outLen - 1);
    memcpy(out, resp, n);
    out[n] = '\0';
    invalidateRuntimeCache_();
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::runtimeUiValues_(const RuntimeUiId* ids,
                                          uint8_t count,
                                          uint8_t* out,
                                          size_t outLen,
                                          size_t* writtenOut)
{
    if (writtenOut) *writtenOut = 0U;
    if (!out || outLen == 0U) return false;
    out[0] = 0U;
    if (!beginRequestSession_(pdMS_TO_TICKS(1500), true)) return false;

    if (!ensureReady_()) {
        LOGW("runtimeUiValues failed: link not ready");
        endRequestSession_();
        return false;
    }
    if (!ids || count == 0U) {
        if (writtenOut) *writtenOut = 0U;
        endRequestSession_();
        return true;
    }

    const size_t reqLen = 1U + ((size_t)count * 2U);
    if (reqLen > I2cCfgProtocol::MaxPayload) {
        LOGW("runtimeUiValues request too large count=%u", (unsigned)count);
        endRequestSession_();
        return false;
    }

    uint8_t req[I2cCfgProtocol::MaxPayload] = {0};
    req[0] = count;
    for (uint8_t i = 0; i < count; ++i) {
        const size_t offset = 1U + ((size_t)i * 2U);
        req[offset] = (uint8_t)(ids[i] & 0xFFU);
        req[offset + 1U] = (uint8_t)((ids[i] >> 8) & 0xFFU);
    }

    uint8_t status = I2cCfgProtocol::StatusFailed;
    size_t respLen = 0U;
    const bool ok = transact_(I2cCfgProtocol::OpGetRuntimeUiValues,
                              req,
                              reqLen,
                              status,
                              out,
                              outLen,
                              respLen);
    if (!ok || status != I2cCfgProtocol::StatusOk) {
        markRemoteUnavailable_();
        LOGW("runtimeUiValues failed transport=%s status=%u (%s) count=%u resp_len=%u",
             ok ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)count,
             (unsigned)respLen);
        endRequestSession_();
        return false;
    }

    markRemoteAvailable_();
    if (writtenOut) *writtenOut = respLen;
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::runtimeAlarmSnapshotJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';
    (void)writeErrorJson(out, outLen, ErrorCode::Disabled, "flowcfg.runtime_alarm.disabled");
    return false;
}

bool I2CCfgClientModule::runtimeStatusJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';

    static constexpr FlowStatusDomain kDomains[] = {
        FlowStatusDomain::System,
        FlowStatusDomain::Wifi,
        FlowStatusDomain::Mqtt,
        FlowStatusDomain::I2c,
        FlowStatusDomain::Pool,
    };

    char domainBuf[kRuntimeStatusDomainBufSize] = {0};
    size_t pos = 0;
    if (!appendTextChecked_(out, outLen, pos, "{\"ok\":true")) {
        (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.runtime_status.aggregate");
        return false;
    }

    for (size_t i = 0; i < (sizeof(kDomains) / sizeof(kDomains[0])); ++i) {
        memset(domainBuf, 0, sizeof(domainBuf));
        if (!runtimeStatusDomainJson_(kDomains[i], domainBuf, sizeof(domainBuf))) {
            if (domainBuf[0] != '\0') {
                snprintf(out, outLen, "%s", domainBuf);
            } else {
                (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.aggregate");
            }
            return false;
        }
        if (!appendDomainFields_(out, outLen, pos, domainBuf)) {
            (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.runtime_status.aggregate");
            return false;
        }
    }

    if (!appendTextChecked_(out, outLen, pos, "}")) {
        (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.runtime_status.aggregate");
        return false;
    }
    return true;
}

bool I2CCfgClientModule::runtimeStatusDomainJson_(FlowStatusDomain domain, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';
    if (domain == FlowStatusDomain::Alarm) {
        (void)writeErrorJson(out, outLen, ErrorCode::Disabled, "flowcfg.runtime_status.alarm.disabled");
        return false;
    }
    if (!runtimeCacheMutex_) return false;
    const uint8_t idx = runtimeStatusDomainCacheIndex_(domain);
    if (xSemaphoreTake(runtimeCacheMutex_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool ok = idx < (sizeof(runtimeStatusDomainCacheValid_) / sizeof(runtimeStatusDomainCacheValid_[0])) &&
                    runtimeStatusDomainCacheValid_[idx] &&
                    runtimeStatusDomainCache_[idx][0] != '\0';
    if (ok) {
        snprintf(out, outLen, "%s", runtimeStatusDomainCache_[idx]);
    }
    xSemaphoreGive(runtimeCacheMutex_);
    if (!ok) {
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.cache");
    }
    return ok;
}

bool I2CCfgClientModule::fetchRuntimeStatusDomainUncached_(FlowStatusDomain domain, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    if (!beginRequestSession_(0, false)) return false;
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.runtime_status");
        endRequestSession_();
        return false;
    }

    const uint8_t domainId = (uint8_t)domain;
    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;
    const uint8_t beginReq[1] = {domainId};
    const bool okBegin = transact_(I2cCfgProtocol::OpGetRuntimeStatusBegin,
                                   beginReq,
                                   sizeof(beginReq),
                                   status,
                                   resp,
                                   sizeof(resp),
                                   respLen);
    if (!okBegin || status != I2cCfgProtocol::StatusOk || respLen < 3) {
        LOGW("runtimeStatus domain=%s failed step=begin transport=%s status=%u (%s) resp_len=%u",
             statusDomainName(domain),
             okBegin ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen);
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.begin");
        endRequestSession_();
        return false;
    }

    const size_t totalLen = (size_t)resp[0] | ((size_t)resp[1] << 8);
    const bool isTruncated = (resp[2] & 0x02u) != 0;
    if (totalLen + 1 > outLen) {
        (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.runtime_status.len");
        endRequestSession_();
        return false;
    }

    size_t written = 0;
    while (written < totalLen) {
        const size_t remain = totalLen - written;
        const uint8_t want = (uint8_t)((remain > I2cCfgProtocol::MaxPayload) ? I2cCfgProtocol::MaxPayload : remain);
        const uint8_t req[3] = {
            (uint8_t)(written & 0xFFu),
            (uint8_t)((written >> 8) & 0xFFu),
            want
        };
        memset(resp, 0, sizeof(resp));
        respLen = 0;
        const bool okChunk = transact_(I2cCfgProtocol::OpGetRuntimeStatusChunk,
                                       req,
                                       sizeof(req),
                                       status,
                                       resp,
                                       sizeof(resp),
                                       respLen);
        if (!okChunk || status != I2cCfgProtocol::StatusOk) {
            LOGW("runtimeStatus domain=%s failed step=chunk off=%u want=%u transport=%s status=%u (%s) resp_len=%u",
                 statusDomainName(domain),
                 (unsigned)written,
                 (unsigned)want,
                 okChunk ? "ok" : "failed",
                 (unsigned)status,
                 statusName(status),
                 (unsigned)respLen);
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.chunk");
            endRequestSession_();
            return false;
        }
        if (respLen == 0 || (written + respLen) > totalLen) {
            LOGW("runtimeStatus domain=%s invalid chunk off=%u resp_len=%u total=%u",
                 statusDomainName(domain),
                 (unsigned)written,
                 (unsigned)respLen,
                 (unsigned)totalLen);
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.chunk_len");
            endRequestSession_();
            return false;
        }
        memcpy(out + written, resp, respLen);
        written += respLen;
    }
    out[written] = '\0';
    if (isTruncated) {
        LOGW("runtimeStatus domain=%s truncated bytes=%u", statusDomainName(domain), (unsigned)written);
    }
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::executeSystemActionJson_(uint8_t action, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    if (!beginRequestSession_(pdMS_TO_TICKS(1500), true)) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flow.system.busy");
        return false;
    }
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flow.system");
        endRequestSession_();
        return false;
    }

    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;
    const uint8_t req[1] = {action};
    const bool ok = transact_(I2cCfgProtocol::OpSystemAction, req, sizeof(req), status, resp, sizeof(resp), respLen);
    if (!ok || status != I2cCfgProtocol::StatusOk) {
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flow.system");
        endRequestSession_();
        return false;
    }

    if (respLen == 0) {
        (void)writeOkJson(out, outLen, "flow.system");
        endRequestSession_();
        return true;
    }

    const size_t n = (respLen < (outLen - 1)) ? respLen : (outLen - 1);
    memcpy(out, resp, n);
    out[n] = '\0';
    endRequestSession_();
    return true;
}

bool I2CCfgClientModule::cmdFlowReboot_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(userCtx);
    if (!self) return false;
    return self->executeSystemActionJson_(1, reply, replyLen);
}

bool I2CCfgClientModule::cmdFlowFactoryReset_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(userCtx);
    if (!self) return false;
    return self->executeSystemActionJson_(2, reply, replyLen);
}

bool I2CCfgClientModule::isReadySvc_()
{
    return ensureReady_();
}
