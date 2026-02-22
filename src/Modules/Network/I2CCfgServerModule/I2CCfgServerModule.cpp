/**
 * @file I2CCfgServerModule.cpp
 * @brief Flow.IO-side I2C cfg server implementation.
 */

#include "I2CCfgServerModule.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemStats.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#define LOG_TAG "I2CfgSrv"
#include "Core/ModuleLog.h"

#include <WiFi.h>
#include <stdlib.h>
#include <string.h>

#ifndef FIRMW
#define FIRMW "unknown"
#endif

namespace {

bool extractIntField(const char* json, const char* key, int32_t& out)
{
    if (!json || !key || key[0] == '\0') return false;

    char pattern[32] = {0};
    const int pn = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (pn <= 0 || (size_t)pn >= sizeof(pattern)) return false;

    const char* p = strstr(json, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t') ++p;

    char* end = nullptr;
    const long v = strtol(p, &end, 10);
    if (!end || end == p) return false;

    out = (int32_t)v;
    return true;
}

size_t tokenLenToSlash_(const char* s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0' && s[n] != '/') ++n;
    return n;
}

bool childTokenForPrefix_(const char* module,
                          const char* prefix,
                          size_t prefixLen,
                          const char*& childStart,
                          size_t& childLen,
                          bool& isExact)
{
    childStart = nullptr;
    childLen = 0;
    isExact = false;
    if (!module || module[0] == '\0') return false;

    if (prefixLen == 0) {
        childStart = module;
        childLen = tokenLenToSlash_(module);
        return childLen > 0;
    }

    if (strncmp(module, prefix, prefixLen) != 0) return false;
    const char sep = module[prefixLen];
    if (sep == '\0') {
        isExact = true;
        return false;
    }
    if (sep != '/') return false;

    childStart = module + prefixLen + 1;
    childLen = tokenLenToSlash_(childStart);
    return childLen > 0;
}

bool tokensEqual_(const char* a, size_t aLen, const char* b, size_t bLen)
{
    if (!a || !b) return false;
    if (aLen != bLen) return false;
    if (aLen == 0) return true;
    return strncmp(a, b, aLen) == 0;
}

bool ipToText_(const IpV4& ip, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    const int n = snprintf(out,
                           outLen,
                           "%u.%u.%u.%u",
                           (unsigned)ip.b[0],
                           (unsigned)ip.b[1],
                           (unsigned)ip.b[2],
                           (unsigned)ip.b[3]);
    return n > 0 && (size_t)n < outLen;
}

}  // namespace

void I2CCfgServerModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::I2cCfg;
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::I2cCfgServer;

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(useIoBusVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(busVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sdaVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sclVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(freqVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(addrVar_, kCfgModuleId, kCfgBranchId);

    logHub_ = services.get<LogHubService>("loghub");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    cmdSvc_ = services.get<CommandService>("cmd");
    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    cfgStore_ = &cfg;
    (void)logHub_;

    resetPatchState_();
    LOGI("I2C cfg server config registered");
}

void I2CCfgServerModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    startLink_();
}

void I2CCfgServerModule::startLink_()
{
    if (started_) return;
    if (!cfgData_.enabled) {
        LOGI("I2C cfg server disabled");
        return;
    }
    if (!cfgSvc_ || !cfgSvc_->applyJson || !cfgSvc_->listModules || !cfgSvc_->toJsonModule) {
        LOGW("I2C cfg server not ready (config service missing)");
        return;
    }
    uint8_t bus = (uint8_t)(cfgData_.bus <= 0 ? 0 : 1);
    int32_t sda = cfgData_.sda;
    int32_t scl = cfgData_.scl;
    if (cfgData_.useIoBus) {
        int32_t ioSda = sda;
        int32_t ioScl = scl;
        if (resolveIoPins_(ioSda, ioScl)) {
            sda = ioSda;
            scl = ioScl;
        } else {
            LOGW("use_io_bus enabled but io config unavailable; fallback sda=%ld scl=%ld",
                 (long)sda,
                 (long)scl);
        }
    }

    if (!link_.beginSlave(bus,
                          cfgData_.address,
                          sda,
                          scl,
                          (uint32_t)(cfgData_.freqHz <= 0 ? 100000 : cfgData_.freqHz))) {
        LOGE("I2C cfg server start failed");
        return;
    }
    link_.setSlaveCallbacks(onReceiveStatic_, onRequestStatic_, this);
    started_ = true;
    ensureActionTask_();
    LOGI("I2C cfg server started app_role=server i2c_role=slave addr=0x%02X bus=%u sda=%ld scl=%ld freq=%ld use_io_bus=%s",
         (unsigned)cfgData_.address,
         (unsigned)bus,
         (long)sda,
         (long)scl,
         (long)cfgData_.freqHz,
         cfgData_.useIoBus ? "true" : "false");
}

void I2CCfgServerModule::resetPatchState_()
{
    patchExpected_ = 0;
    patchWritten_ = 0;
    patchBuf_[0] = '\0';
}

bool I2CCfgServerModule::buildRuntimeStatusJson_(bool& truncatedOut)
{
    truncatedOut = false;

    const bool wifiUp = dataStore_ ? wifiReady(*dataStore_) : false;
    const bool mqttUp = dataStore_ ? mqttReady(*dataStore_) : false;
    const IpV4 ip = dataStore_ ? wifiIp(*dataStore_) : IpV4{0, 0, 0, 0};
    const uint32_t mqttRxDropCnt = dataStore_ ? mqttRxDrop(*dataStore_) : 0;
    const uint32_t mqttParseFailCnt = dataStore_ ? mqttParseFail(*dataStore_) : 0;
    const uint32_t mqttHandlerFailCnt = dataStore_ ? mqttHandlerFail(*dataStore_) : 0;
    const uint32_t mqttOversizeDropCnt = dataStore_ ? mqttOversizeDrop(*dataStore_) : 0;

    char ipTxt[20] = {0};
    if (!ipToText_(ip, ipTxt, sizeof(ipTxt))) {
        snprintf(ipTxt, sizeof(ipTxt), "0.0.0.0");
    }

    int32_t rssi = 0;
    bool hasRssi = false;
    if (wifiUp && WiFi.status() == WL_CONNECTED) {
        rssi = (int32_t)WiFi.RSSI();
        hasRssi = true;
    }

    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    const uint32_t nowMs = millis();
    const bool hasSupervisorSeen = reqCount_ > 0;
    const uint32_t lastReqAgoMs = hasSupervisorSeen ? (nowMs - lastReqMs_) : 0U;
    const bool supervisorLinkOk = started_ && hasSupervisorSeen && (lastReqAgoMs <= 15000U);

    const int n = snprintf(
        statusJson_,
        sizeof(statusJson_),
        "{\"ok\":true,"
        "\"firmware\":\"%s\","
        "\"uptime_ms\":%llu,"
        "\"heap\":{\"free\":%lu,\"min\":%lu,\"largest\":%lu,\"frag\":%u},"
        "\"wifi\":{\"ready\":%s,\"ip\":\"%s\",\"rssi_dbm\":%ld,\"has_rssi\":%s},"
        "\"mqtt\":{\"ready\":%s,\"rx_drop\":%lu,\"parse_fail\":%lu,\"handler_fail\":%lu,\"oversize_drop\":%lu},"
        "\"i2c\":{\"enabled\":%s,\"started\":%s,\"address\":%u,\"request_count\":%lu,"
        "\"bad_request_count\":%lu,\"supervisor_seen\":%s,\"last_request_ago_ms\":%lu,\"supervisor_link_ok\":%s}}",
        FIRMW,
        (unsigned long long)snap.uptimeMs64,
        (unsigned long)snap.heap.freeBytes,
        (unsigned long)snap.heap.minFreeBytes,
        (unsigned long)snap.heap.largestFreeBlock,
        (unsigned)snap.heap.fragPercent,
        wifiUp ? "true" : "false",
        ipTxt,
        (long)rssi,
        hasRssi ? "true" : "false",
        mqttUp ? "true" : "false",
        (unsigned long)mqttRxDropCnt,
        (unsigned long)mqttParseFailCnt,
        (unsigned long)mqttHandlerFailCnt,
        (unsigned long)mqttOversizeDropCnt,
        cfgData_.enabled ? "true" : "false",
        started_ ? "true" : "false",
        (unsigned)cfgData_.address,
        (unsigned long)reqCount_,
        (unsigned long)badReqCount_,
        hasSupervisorSeen ? "true" : "false",
        (unsigned long)lastReqAgoMs,
        supervisorLinkOk ? "true" : "false");
    if (n <= 0) return false;
    if ((size_t)n >= sizeof(statusJson_)) {
        statusJson_[sizeof(statusJson_) - 1] = '\0';
        truncatedOut = true;
    }
    return true;
}

void I2CCfgServerModule::ensureActionTask_()
{
    if (actionTask_) return;
    const BaseType_t ok = xTaskCreatePinnedToCore(
        &I2CCfgServerModule::actionTaskStatic_,
        "I2CfgAct",
        3072,
        this,
        1,
        &actionTask_,
        0);
    if (ok != pdPASS) {
        actionTask_ = nullptr;
        LOGW("Failed to start system action task");
    }
}

void I2CCfgServerModule::queueSystemAction_(PendingSystemAction action)
{
    if (action == PendingSystemAction::None) return;
    portENTER_CRITICAL(&actionMux_);
    pendingAction_ = (uint8_t)action;
    portEXIT_CRITICAL(&actionMux_);
    ensureActionTask_();
}

I2CCfgServerModule::PendingSystemAction I2CCfgServerModule::takePendingSystemAction_()
{
    portENTER_CRITICAL(&actionMux_);
    const uint8_t raw = pendingAction_;
    pendingAction_ = (uint8_t)PendingSystemAction::None;
    portEXIT_CRITICAL(&actionMux_);
    return (PendingSystemAction)raw;
}

void I2CCfgServerModule::actionTaskStatic_(void* ctx)
{
    I2CCfgServerModule* self = static_cast<I2CCfgServerModule*>(ctx);
    if (!self) {
        vTaskDelete(nullptr);
        return;
    }
    self->actionLoop_();
}

void I2CCfgServerModule::actionLoop_()
{
    while (true) {
        const PendingSystemAction action = takePendingSystemAction_();
        if (action == PendingSystemAction::None) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!cmdSvc_ || !cmdSvc_->execute) {
            LOGW("system action ignored (cmd service unavailable)");
            continue;
        }

        const char* cmd = nullptr;
        if (action == PendingSystemAction::Reboot) {
            cmd = "system.reboot";
        } else if (action == PendingSystemAction::FactoryReset) {
            cmd = "system.factory_reset";
        }
        if (!cmd) continue;

        char reply[128] = {0};
        const bool ok = cmdSvc_->execute(cmdSvc_->ctx, cmd, "{}", nullptr, reply, sizeof(reply));
        LOGI("executed queued action cmd=%s ok=%d reply=%s", cmd, (int)ok, reply[0] ? reply : "{}");
    }
}

bool I2CCfgServerModule::resolveIoPins_(int32_t& sdaOut, int32_t& sclOut) const
{
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;

    char ioJson[320] = {0};
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx, "io", ioJson, sizeof(ioJson), &truncated)) {
        return false;
    }
    (void)truncated;

    int32_t parsedSda = sdaOut;
    int32_t parsedScl = sclOut;
    const bool okSda = extractIntField(ioJson, "i2c_sda", parsedSda);
    const bool okScl = extractIntField(ioJson, "i2c_scl", parsedScl);
    if (!okSda || !okScl) return false;

    sdaOut = parsedSda;
    sclOut = parsedScl;
    return true;
}

void I2CCfgServerModule::onReceiveStatic_(void* ctx, const uint8_t* data, size_t len)
{
    I2CCfgServerModule* self = static_cast<I2CCfgServerModule*>(ctx);
    if (!self) return;
    self->onReceive_(data, len);
}

size_t I2CCfgServerModule::onRequestStatic_(void* ctx, uint8_t* out, size_t maxLen)
{
    I2CCfgServerModule* self = static_cast<I2CCfgServerModule*>(ctx);
    if (!self) return 0;
    return self->onRequest_(out, maxLen);
}

void I2CCfgServerModule::onReceive_(const uint8_t* data, size_t len)
{
    if (!data || len < I2cCfgProtocol::ReqHeaderSize) return;
    if (data[0] != I2cCfgProtocol::ReqMagic || data[1] != I2cCfgProtocol::Version) {
        ++badReqCount_;
        buildResponse_(0, 0, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
        return;
    }

    const uint8_t op = data[2];
    const uint8_t seq = data[3];
    const size_t payloadLen = data[4];
    if (payloadLen > I2cCfgProtocol::MaxPayload || len != (I2cCfgProtocol::ReqHeaderSize + payloadLen)) {
        ++badReqCount_;
        buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
        return;
    }
    ++reqCount_;
    lastReqMs_ = millis();
    handleRequest_(op, seq, data + I2cCfgProtocol::ReqHeaderSize, payloadLen);
}

size_t I2CCfgServerModule::onRequest_(uint8_t* out, size_t maxLen)
{
    if (!out || maxLen == 0) return 0;
    size_t n = 0;
    portENTER_CRITICAL(&txMux_);
    n = txFrameLen_;
    if (n > maxLen) n = maxLen;
    if (n > 0) memcpy(out, txFrame_, n);
    portEXIT_CRITICAL(&txMux_);
    return n;
}

void I2CCfgServerModule::buildResponse_(uint8_t op,
                                        uint8_t seq,
                                        uint8_t status,
                                        const uint8_t* payload,
                                        size_t payloadLen)
{
    if (payloadLen > I2cCfgProtocol::MaxPayload) payloadLen = I2cCfgProtocol::MaxPayload;
    const size_t total = I2cCfgProtocol::RespHeaderSize + payloadLen;
    portENTER_CRITICAL(&txMux_);
    txFrame_[0] = I2cCfgProtocol::RespMagic;
    txFrame_[1] = I2cCfgProtocol::Version;
    txFrame_[2] = op;
    txFrame_[3] = seq;
    txFrame_[4] = status;
    txFrame_[5] = (uint8_t)payloadLen;
    if (payload && payloadLen > 0) memcpy(txFrame_ + I2cCfgProtocol::RespHeaderSize, payload, payloadLen);
    txFrameLen_ = total;
    portEXIT_CRITICAL(&txMux_);
}

void I2CCfgServerModule::handleRequest_(uint8_t op, uint8_t seq, const uint8_t* payload, size_t payloadLen)
{
    if (!cfgSvc_) {
        buildResponse_(op, seq, I2cCfgProtocol::StatusNotReady, nullptr, 0);
        return;
    }

    if (op != I2cCfgProtocol::OpGetModuleBegin && op != I2cCfgProtocol::OpGetModuleChunk) {
        moduleJsonLen_ = 0;
        moduleJsonValid_ = false;
        moduleJsonTruncated_ = false;
    }
    if (op != I2cCfgProtocol::OpGetRuntimeStatusBegin && op != I2cCfgProtocol::OpGetRuntimeStatusChunk) {
        statusJsonLen_ = 0;
        statusJsonValid_ = false;
        statusJsonTruncated_ = false;
    }
    if (op != I2cCfgProtocol::OpPatchBegin &&
        op != I2cCfgProtocol::OpPatchWrite &&
        op != I2cCfgProtocol::OpPatchCommit) {
        resetPatchState_();
    }

    if (op == I2cCfgProtocol::OpPing) {
        const uint8_t pong[2] = {1, (uint8_t)cfgData_.address};
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, pong, sizeof(pong));
        return;
    }

    if (op == I2cCfgProtocol::OpListCount) {
        const char* modules[Limits::Mqtt::Capacity::CfgTopicMax] = {0};
        const uint8_t count = cfgSvc_->listModules
                                  ? cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Mqtt::Capacity::CfgTopicMax)
                                  : 0;
        const uint8_t out[1] = {count};
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, out, sizeof(out));
        return;
    }

    if (op == I2cCfgProtocol::OpListItem) {
        if (payloadLen < 1 || !cfgSvc_->listModules) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const uint8_t idx = payload[0];
        const char* modules[Limits::Mqtt::Capacity::CfgTopicMax] = {0};
        const uint8_t count = cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Mqtt::Capacity::CfgTopicMax);
        if (idx >= count || !modules[idx]) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusRange, nullptr, 0);
            return;
        }
        const char* name = modules[idx];
        const size_t n = strnlen(name, I2cCfgProtocol::MaxPayload);
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, (const uint8_t*)name, n);
        return;
    }

    if (op == I2cCfgProtocol::OpListChildrenCount) {
        char prefix[40] = {0};
        const size_t n = (payloadLen < (sizeof(prefix) - 1)) ? payloadLen : (sizeof(prefix) - 1);
        if (n > 0) memcpy(prefix, payload, n);
        prefix[n] = '\0';

        size_t prefixLen = strnlen(prefix, sizeof(prefix));
        while (prefixLen > 0 && prefix[0] == '/') {
            memmove(prefix, prefix + 1, prefixLen);
            --prefixLen;
        }
        while (prefixLen > 0 && prefix[prefixLen - 1] == '/') {
            prefix[prefixLen - 1] = '\0';
            --prefixLen;
        }

        const char* modules[Limits::Mqtt::Capacity::CfgTopicMax] = {0};
        const uint8_t countModules =
            cfgSvc_->listModules ? cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Mqtt::Capacity::CfgTopicMax) : 0;

        uint8_t childCount = 0;
        bool hasExact = false;
        for (uint8_t i = 0; i < countModules; ++i) {
            const char* childStart = nullptr;
            size_t childLen = 0;
            bool exact = false;
            if (!childTokenForPrefix_(modules[i], prefix, prefixLen, childStart, childLen, exact)) {
                if (exact) hasExact = true;
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < i; ++j) {
                const char* prevStart = nullptr;
                size_t prevLen = 0;
                bool prevExact = false;
                if (!childTokenForPrefix_(modules[j], prefix, prefixLen, prevStart, prevLen, prevExact)) {
                    continue;
                }
                if (tokensEqual_(childStart, childLen, prevStart, prevLen)) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) ++childCount;
        }

        const uint8_t out[2] = {childCount, (uint8_t)(hasExact ? 1U : 0U)};
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, out, sizeof(out));
        return;
    }

    if (op == I2cCfgProtocol::OpListChildrenItem) {
        if (payloadLen < 1 || !cfgSvc_->listModules) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const uint8_t wantedIdx = payload[0];

        char prefix[40] = {0};
        const size_t rawPrefixLen = payloadLen - 1;
        const size_t n = (rawPrefixLen < (sizeof(prefix) - 1)) ? rawPrefixLen : (sizeof(prefix) - 1);
        if (n > 0) memcpy(prefix, payload + 1, n);
        prefix[n] = '\0';

        size_t prefixLen = strnlen(prefix, sizeof(prefix));
        while (prefixLen > 0 && prefix[0] == '/') {
            memmove(prefix, prefix + 1, prefixLen);
            --prefixLen;
        }
        while (prefixLen > 0 && prefix[prefixLen - 1] == '/') {
            prefix[prefixLen - 1] = '\0';
            --prefixLen;
        }

        const char* modules[Limits::Mqtt::Capacity::CfgTopicMax] = {0};
        const uint8_t countModules =
            cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Mqtt::Capacity::CfgTopicMax);

        uint8_t childRank = 0;
        for (uint8_t i = 0; i < countModules; ++i) {
            const char* childStart = nullptr;
            size_t childLen = 0;
            bool exact = false;
            if (!childTokenForPrefix_(modules[i], prefix, prefixLen, childStart, childLen, exact)) {
                continue;
            }

            bool duplicate = false;
            for (uint8_t j = 0; j < i; ++j) {
                const char* prevStart = nullptr;
                size_t prevLen = 0;
                bool prevExact = false;
                if (!childTokenForPrefix_(modules[j], prefix, prefixLen, prevStart, prevLen, prevExact)) {
                    continue;
                }
                if (tokensEqual_(childStart, childLen, prevStart, prevLen)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;

            if (childRank == wantedIdx) {
                const size_t childOutLen = (childLen < I2cCfgProtocol::MaxPayload) ? childLen : I2cCfgProtocol::MaxPayload;
                buildResponse_(op, seq, I2cCfgProtocol::StatusOk, (const uint8_t*)childStart, childOutLen);
                return;
            }
            ++childRank;
        }

        buildResponse_(op, seq, I2cCfgProtocol::StatusRange, nullptr, 0);
        return;
    }

    if (op == I2cCfgProtocol::OpGetModuleBegin) {
        if (payloadLen == 0 || !cfgSvc_->toJsonModule) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        char module[40] = {0};
        const size_t n = (payloadLen < (sizeof(module) - 1)) ? payloadLen : (sizeof(module) - 1);
        memcpy(module, payload, n);
        module[n] = '\0';
        if (module[0] == '\0') {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }

        bool truncated = false;
        const bool exportRawSecrets = (strcmp(module, "wifi") == 0);
        const bool ok = cfgStore_
                            ? cfgStore_->toJsonModule(module,
                                                      moduleJson_,
                                                      sizeof(moduleJson_),
                                                      &truncated,
                                                      !exportRawSecrets)
                            : cfgSvc_->toJsonModule(
                                  cfgSvc_->ctx, module, moduleJson_, sizeof(moduleJson_), &truncated);
        if (!ok) {
            moduleJsonLen_ = 0;
            moduleJsonValid_ = false;
            moduleJsonTruncated_ = false;
            buildResponse_(op, seq, I2cCfgProtocol::StatusRange, nullptr, 0);
            return;
        }
        if (exportRawSecrets) {
            LOGW("flowcfg.get module=wifi exported with clear password (debug/sync path)");
        }
        moduleJsonLen_ = strnlen(moduleJson_, sizeof(moduleJson_));
        moduleJsonValid_ = true;
        moduleJsonTruncated_ = truncated;

        uint8_t out[3] = {0};
        out[0] = (uint8_t)(moduleJsonLen_ & 0xFFu);
        out[1] = (uint8_t)((moduleJsonLen_ >> 8) & 0xFFu);
        out[2] = moduleJsonTruncated_ ? 0x02u : 0x00u;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, out, sizeof(out));
        return;
    }

    if (op == I2cCfgProtocol::OpGetModuleChunk) {
        if (!moduleJsonValid_ || payloadLen < 3) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const size_t offset = (size_t)payload[0] | ((size_t)payload[1] << 8);
        size_t want = (size_t)payload[2];
        if (offset > moduleJsonLen_) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusRange, nullptr, 0);
            return;
        }
        if (want == 0 || want > I2cCfgProtocol::MaxPayload) want = I2cCfgProtocol::MaxPayload;
        const size_t avail = moduleJsonLen_ - offset;
        const size_t n = (avail < want) ? avail : want;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, (const uint8_t*)(moduleJson_ + offset), n);
        if ((offset + n) >= moduleJsonLen_) {
            moduleJsonLen_ = 0;
            moduleJsonValid_ = false;
            moduleJsonTruncated_ = false;
        }
        return;
    }

    if (op == I2cCfgProtocol::OpGetRuntimeStatusBegin) {
        bool truncated = false;
        if (!buildRuntimeStatusJson_(truncated)) {
            statusJsonLen_ = 0;
            statusJsonValid_ = false;
            statusJsonTruncated_ = false;
            buildResponse_(op, seq, I2cCfgProtocol::StatusFailed, nullptr, 0);
            return;
        }
        statusJsonLen_ = strnlen(statusJson_, sizeof(statusJson_));
        statusJsonValid_ = true;
        statusJsonTruncated_ = truncated;

        uint8_t out[3] = {0};
        out[0] = (uint8_t)(statusJsonLen_ & 0xFFu);
        out[1] = (uint8_t)((statusJsonLen_ >> 8) & 0xFFu);
        out[2] = statusJsonTruncated_ ? 0x02u : 0x00u;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, out, sizeof(out));
        return;
    }

    if (op == I2cCfgProtocol::OpGetRuntimeStatusChunk) {
        if (!statusJsonValid_ || payloadLen < 3) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const size_t offset = (size_t)payload[0] | ((size_t)payload[1] << 8);
        size_t want = (size_t)payload[2];
        if (offset > statusJsonLen_) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusRange, nullptr, 0);
            return;
        }
        if (want == 0 || want > I2cCfgProtocol::MaxPayload) want = I2cCfgProtocol::MaxPayload;
        const size_t avail = statusJsonLen_ - offset;
        const size_t n = (avail < want) ? avail : want;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, (const uint8_t*)(statusJson_ + offset), n);
        if ((offset + n) >= statusJsonLen_) {
            statusJsonLen_ = 0;
            statusJsonValid_ = false;
            statusJsonTruncated_ = false;
        }
        return;
    }

    if (op == I2cCfgProtocol::OpPatchBegin) {
        if (payloadLen < 2) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const size_t total = (size_t)payload[0] | ((size_t)payload[1] << 8);
        if (total == 0 || total >= sizeof(patchBuf_) || total > Limits::JsonConfigApplyBuf) {
            resetPatchState_();
            buildResponse_(op, seq, I2cCfgProtocol::StatusOverflow, nullptr, 0);
            return;
        }
        resetPatchState_();
        patchExpected_ = total;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, nullptr, 0);
        return;
    }

    if (op == I2cCfgProtocol::OpPatchWrite) {
        if (payloadLen < 2 || patchExpected_ == 0) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const size_t offset = (size_t)payload[0] | ((size_t)payload[1] << 8);
        const size_t n = payloadLen - 2;
        if (offset != patchWritten_) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusRange, nullptr, 0);
            return;
        }
        if ((patchWritten_ + n) > patchExpected_ || (patchWritten_ + n) > Limits::JsonConfigApplyBuf) {
            resetPatchState_();
            buildResponse_(op, seq, I2cCfgProtocol::StatusOverflow, nullptr, 0);
            return;
        }
        memcpy(patchBuf_ + patchWritten_, payload + 2, n);
        patchWritten_ += n;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, nullptr, 0);
        return;
    }

    if (op == I2cCfgProtocol::OpPatchCommit) {
        if (!cfgSvc_->applyJson || patchExpected_ == 0 || patchWritten_ != patchExpected_) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        patchBuf_[patchExpected_] = '\0';
        const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, patchBuf_);

        char ack[96] = {0};
        if (ok) {
            (void)writeOkJson(ack, sizeof(ack), "i2c/cfg/apply");
        } else {
            (void)writeErrorJson(ack, sizeof(ack), ErrorCode::CfgApplyFailed, "i2c/cfg/apply");
        }
        const size_t n = strnlen(ack, sizeof(ack));
        buildResponse_(op, seq, ok ? I2cCfgProtocol::StatusOk : I2cCfgProtocol::StatusFailed, (const uint8_t*)ack, n);
        resetPatchState_();
        return;
    }

    if (op == I2cCfgProtocol::OpSystemAction) {
        if (payloadLen < 1) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        PendingSystemAction action = PendingSystemAction::None;
        const uint8_t actionId = payload[0];
        if (actionId == 1) {
            action = PendingSystemAction::Reboot;
        } else if (actionId == 2) {
            action = PendingSystemAction::FactoryReset;
        } else {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }

        const char* actionTxt = (action == PendingSystemAction::Reboot) ? "reboot" : "factory_reset";
        char ack[80] = {0};
        const int n = snprintf(ack,
                               sizeof(ack),
                               "{\"ok\":true,\"queued\":true,\"action\":\"%s\"}",
                               actionTxt);
        const size_t ackLen = (n > 0 && (size_t)n < sizeof(ack)) ? (size_t)n : 0;
        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, (const uint8_t*)ack, ackLen);
        queueSystemAction_(action);
        LOGW("queued remote system action=%s via I2C", actionTxt);
        return;
    }

    buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
}
