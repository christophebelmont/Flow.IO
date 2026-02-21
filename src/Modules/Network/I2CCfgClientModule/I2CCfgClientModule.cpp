/**
 * @file I2CCfgClientModule.cpp
 * @brief Supervisor-side I2C cfg client implementation.
 */

#include "I2CCfgClientModule.h"
#include "Core/ErrorCodes.h"
#define LOG_TAG "I2CfgCli"
#include "Core/ModuleLog.h"

#include <stdlib.h>
#include <string.h>

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
        case I2cCfgProtocol::OpPatchBegin: return "patch_begin";
        case I2cCfgProtocol::OpPatchWrite: return "patch_write";
        case I2cCfgProtocol::OpPatchCommit: return "patch_commit";
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

}  // namespace

void I2CCfgClientModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::I2cCfg;
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::I2cCfgClient;

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(useIoBusVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(busVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sdaVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sclVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(freqVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(addrVar_, kCfgModuleId, kCfgBranchId);

    logHub_ = services.get<LogHubService>("loghub");
    cfgSvc_ = services.get<ConfigStoreService>("config");
    (void)services.add("flowcfg", &svc_);
    LOGI("I2C cfg client config/service registered");
    (void)logHub_;
}

void I2CCfgClientModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    LOGI("onConfigLoaded enabled=%s use_io_bus=%s bus=%ld sda=%ld scl=%ld freq=%ld target=0x%02X",
         cfgData_.enabled ? "true" : "false",
         cfgData_.useIoBus ? "true" : "false",
         (long)cfgData_.bus,
         (long)cfgData_.sda,
         (long)cfgData_.scl,
         (long)cfgData_.freqHz,
         (unsigned)cfgData_.targetAddr);
    startLink_();
}

void I2CCfgClientModule::startLink_()
{
    LOGI("startLink requested enabled=%s ready=%s",
         cfgData_.enabled ? "true" : "false",
         ready_ ? "true" : "false");
    ready_ = false;
    if (!cfgData_.enabled) {
        LOGI("I2C cfg client disabled");
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

    if (!link_.beginMaster(bus,
                           sda,
                           scl,
                           (uint32_t)(cfgData_.freqHz <= 0 ? 100000 : cfgData_.freqHz))) {
        LOGE("I2C cfg client start failed");
        return;
    }
    ready_ = true;
    LOGI("I2C cfg client started app_role=client i2c_role=master target=0x%02X bus=%u sda=%ld scl=%ld freq=%ld use_io_bus=%s",
         (unsigned)cfgData_.targetAddr,
         (unsigned)bus,
         (long)sda,
         (long)scl,
         (long)cfgData_.freqHz,
         cfgData_.useIoBus ? "true" : "false");

    uint8_t pingStatus = I2cCfgProtocol::StatusFailed;
    if (!pingFlow_(pingStatus)) {
        LOGW("I2C cfg ping transport failed target=0x%02X (check wiring/power/address)",
             (unsigned)cfgData_.targetAddr);
    } else if (pingStatus != I2cCfgProtocol::StatusOk) {
        LOGW("I2C cfg ping returned status=%u (%s) target=0x%02X",
             (unsigned)pingStatus,
             statusName(pingStatus),
             (unsigned)cfgData_.targetAddr);
    } else {
        LOGI("I2C cfg ping ok target=0x%02X", (unsigned)cfgData_.targetAddr);
    }
}

bool I2CCfgClientModule::ensureReady_()
{
    if (ready_) return true;
    if (!cfgData_.enabled) {
        LOGW("ensureReady failed: module disabled");
        return false;
    }
    LOGW("ensureReady: link not ready, attempting restart");
    startLink_();
    return ready_;
}

bool I2CCfgClientModule::resolveIoPins_(int32_t& sdaOut, int32_t& sclOut) const
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

bool I2CCfgClientModule::isReady_() const
{
    return ready_;
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
            LOGW("I2C transfer failed op=%s seq=%u addr=0x%02X req=%u",
                 opName(op),
                 (unsigned)seq,
                 (unsigned)cfgData_.targetAddr,
                 (unsigned)(I2cCfgProtocol::ReqHeaderSize + reqLen));
            return false;
        }
        if (rxLen < I2cCfgProtocol::RespHeaderSize) {
            if (attempt + 1 < kMaxAttempts) {
                delay(2);
                continue;
            }
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
            LOGW("I2C op/seq mismatch expected_op=%u expected_seq=%u got_op=%u got_seq=%u",
                 (unsigned)op,
                 (unsigned)seq,
                 (unsigned)rx[2],
                 (unsigned)rx[3]);
            return false;
        }

        break;
    }

    statusOut = rx[4];
    const size_t payloadLen = rx[5];
    if (payloadLen > I2cCfgProtocol::MaxPayload) {
        LOGW("I2C invalid payload len op=%s seq=%u payload=%u",
             opName(op),
             (unsigned)seq,
             (unsigned)payloadLen);
        return false;
    }
    if (rxLen < (I2cCfgProtocol::RespHeaderSize + payloadLen)) {
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
    if (!ensureReady_()) {
        LOGW("listModules aborted: link not ready");
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.modules");
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
        return false;
    }
    const uint8_t count = resp[0];
    LOGI("flowcfg.list begin count=%u", (unsigned)count);

    size_t pos = 0;
    int wrote = snprintf(out + pos, outLen - pos, "{\"ok\":true,\"modules\":[");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) return false;
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
            return false;
        }

        char moduleName[64] = {0};
        const size_t n = (respLen < (sizeof(moduleName) - 1)) ? respLen : (sizeof(moduleName) - 1);
        memcpy(moduleName, resp, n);
        moduleName[n] = '\0';

        wrote = snprintf(out + pos, outLen - pos, "%s\"%s\"", (i == 0) ? "" : ",", moduleName);
        if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) {
            (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.modules.json");
            return false;
        }
        pos += (size_t)wrote;
    }

    wrote = snprintf(out + pos, outLen - pos, "]}");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) return false;
    LOGI("flowcfg.list done count=%u", (unsigned)count);
    return true;
}

bool I2CCfgClientModule::listChildrenJson_(const char* prefix, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.children");
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
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) return false;
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
            return false;
        }
        pos += (size_t)wrote;
    }

    wrote = snprintf(out + pos, outLen - pos, "]}");
    if (!(wrote > 0 && (size_t)wrote < (outLen - pos))) return false;
    LOGI("flowcfg.children done prefix=%s count=%u",
         prefixLen > 0 ? prefixNorm : "<root>",
         (unsigned)count);
    return true;
}

bool I2CCfgClientModule::getModuleJson_(const char* module, char* out, size_t outLen, bool* truncated)
{
    if (truncated) *truncated = false;
    if (!out || outLen == 0 || !module || module[0] == '\0') return false;
    if (!ensureReady_()) return false;

    const size_t moduleLen = strnlen(module, I2cCfgProtocol::MaxPayload);
    if (moduleLen == 0 || moduleLen >= I2cCfgProtocol::MaxPayload) return false;
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
        return false;
    }

    const size_t totalLen = (size_t)resp[0] | ((size_t)resp[1] << 8);
    const bool isTruncated = (resp[2] & 0x02u) != 0;
    if (truncated) *truncated = isTruncated;
    LOGI("flowcfg.get info module=%s total=%u truncated=%s",
         module,
         (unsigned)totalLen,
         isTruncated ? "true" : "false");

    if (totalLen + 1 > outLen) return false;

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
            return false;
        }
        if (respLen == 0 || (written + respLen) > totalLen) {
            LOGW("getModule invalid chunk module=%s off=%u resp_len=%u total=%u",
                 module,
                 (unsigned)written,
                 (unsigned)respLen,
                 (unsigned)totalLen);
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
    return true;
}

bool I2CCfgClientModule::applyPatchJson_(const char* patch, char* out, size_t outLen)
{
    if (!out || outLen == 0 || !patch) return false;
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.apply");
        return false;
    }

    const size_t len = strnlen(patch, Limits::JsonConfigApplyBuf + 1);
    if (len == 0 || len > Limits::JsonConfigApplyBuf) {
        (void)writeErrorJson(out, outLen, ErrorCode::BadCfgJson, "flowcfg.apply");
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
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.apply.begin");
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
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.apply.write");
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
        (void)writeErrorJson(out, outLen, ErrorCode::CfgApplyFailed, "flowcfg.apply.commit");
        return false;
    }

    if (respLen == 0) {
        (void)writeOkJson(out, outLen, "flowcfg.apply");
        return true;
    }

    const size_t n = (respLen < (outLen - 1)) ? respLen : (outLen - 1);
    memcpy(out, resp, n);
    out[n] = '\0';
    return true;
}

bool I2CCfgClientModule::runtimeStatusJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    if (!ensureReady_()) {
        (void)writeErrorJson(out, outLen, ErrorCode::NotReady, "flowcfg.runtime_status");
        return false;
    }

    uint8_t status = 0;
    uint8_t resp[96] = {0};
    size_t respLen = 0;
    const bool okBegin = transact_(I2cCfgProtocol::OpGetRuntimeStatusBegin,
                                   nullptr,
                                   0,
                                   status,
                                   resp,
                                   sizeof(resp),
                                   respLen);
    if (!okBegin ||
        status != I2cCfgProtocol::StatusOk ||
        respLen < 3) {
        LOGW("runtimeStatus failed step=begin transport=%s status=%u (%s) resp_len=%u",
             okBegin ? "ok" : "failed",
             (unsigned)status,
             statusName(status),
             (unsigned)respLen);
        (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.begin");
        return false;
    }

    const size_t totalLen = (size_t)resp[0] | ((size_t)resp[1] << 8);
    const bool isTruncated = (resp[2] & 0x02u) != 0;
    if (totalLen + 1 > outLen) {
        (void)writeErrorJson(out, outLen, ErrorCode::InternalAckOverflow, "flowcfg.runtime_status.len");
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
            LOGW("runtimeStatus failed step=chunk off=%u want=%u transport=%s status=%u (%s) resp_len=%u",
                 (unsigned)written,
                 (unsigned)want,
                 okChunk ? "ok" : "failed",
                 (unsigned)status,
                 statusName(status),
                 (unsigned)respLen);
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.chunk");
            return false;
        }
        if (respLen == 0 || (written + respLen) > totalLen) {
            LOGW("runtimeStatus invalid chunk off=%u resp_len=%u total=%u",
                 (unsigned)written,
                 (unsigned)respLen,
                 (unsigned)totalLen);
            (void)writeErrorJson(out, outLen, ErrorCode::Failed, "flowcfg.runtime_status.chunk_len");
            return false;
        }
        memcpy(out + written, resp, respLen);
        written += respLen;
    }
    out[written] = '\0';
    if (isTruncated) {
        LOGW("runtimeStatus truncated bytes=%u", (unsigned)written);
    }
    return true;
}

bool I2CCfgClientModule::svcIsReady_(void* ctx)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(ctx);
    return self && self->isReady_();
}

bool I2CCfgClientModule::svcListModulesJson_(void* ctx, char* out, size_t outLen)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(ctx);
    return self ? self->listModulesJson_(out, outLen) : false;
}

bool I2CCfgClientModule::svcListChildrenJson_(void* ctx, const char* prefix, char* out, size_t outLen)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(ctx);
    return self ? self->listChildrenJson_(prefix, out, outLen) : false;
}

bool I2CCfgClientModule::svcGetModuleJson_(void* ctx, const char* module, char* out, size_t outLen, bool* truncated)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(ctx);
    return self ? self->getModuleJson_(module, out, outLen, truncated) : false;
}

bool I2CCfgClientModule::svcRuntimeStatusJson_(void* ctx, char* out, size_t outLen)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(ctx);
    return self ? self->runtimeStatusJson_(out, outLen) : false;
}

bool I2CCfgClientModule::svcApplyPatchJson_(void* ctx, const char* patch, char* out, size_t outLen)
{
    I2CCfgClientModule* self = static_cast<I2CCfgClientModule*>(ctx);
    return self ? self->applyPatchJson_(patch, out, outLen) : false;
}
