/**
 * @file I2CCfgServerModule.cpp
 * @brief Flow.IO-side I2C cfg server implementation.
 */

#include "I2CCfgServerModule.h"
#include "Core/ErrorCodes.h"
#include "Core/FirmwareVersion.h"
#include "Core/SystemStats.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::I2cCfgServerModule)
#include "Core/ModuleLog.h"

#include <WiFi.h>
#include <new>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint8_t kInterlinkBus = 1;  // Interlink is fixed on I2C controller 1 (Wire1 on ESP32).
constexpr uint8_t kI2cServerCfgProducerId = 50;
constexpr uint8_t kI2cServerCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kI2cServerCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::I2cCfg, kI2cServerCfgBranch}, "elink/server", "elink/server", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
constexpr uint8_t kRuntimeUiMaxIdsPerRequest = (uint8_t)((I2cCfgProtocol::MaxPayload - 1U) / 2U);
constexpr uint8_t kI2cRuntimeUiValueLinkOk = 1;
constexpr uint8_t kI2cRuntimeUiValueSeen = 2;
constexpr uint8_t kI2cRuntimeUiValueReqCount = 3;
constexpr uint8_t kI2cRuntimeUiValueBadReqCount = 4;
constexpr uint8_t kI2cRuntimeUiValueLastReqAgoMs = 5;

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

bool findJsonValue_(const char* json, const char* key, const char*& valueOut)
{
    if (!json || !key) return false;
    char pattern[32] = {0};
    const int n = snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    if (n <= 0 || (size_t)n >= sizeof(pattern)) return false;

    const char* value = strstr(json, pattern);
    if (!value) return false;
    value += (size_t)n;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') ++value;
    valueOut = value;
    return true;
}

bool parseJsonBoolField_(const char* json, const char* key, bool& valueOut)
{
    const char* value = nullptr;
    if (!findJsonValue_(json, key, value)) return false;
    if (strncmp(value, "true", 4) == 0) {
        valueOut = true;
        return true;
    }
    if (strncmp(value, "false", 5) == 0) {
        valueOut = false;
        return true;
    }
    return false;
}

bool parseJsonIntField_(const char* json, const char* key, int32_t& valueOut)
{
    const char* value = nullptr;
    if (!findJsonValue_(json, key, value)) return false;

    char* end = nullptr;
    const long parsed = strtol(value, &end, 10);
    if (end == value) return false;
    valueOut = (int32_t)parsed;
    return true;
}

bool extractJsonString_(const char* start, char* out, size_t outLen, const char*& nextOut)
{
    if (!start || !out || outLen == 0) return false;

    size_t pos = 0;
    const char* p = start;
    while (*p != '\0') {
        char c = *p++;
        if (c == '"') {
            out[pos] = '\0';
            nextOut = p;
            return true;
        }
        if (c == '\\') {
            if (*p == '\0') break;
            c = *p++;
        }
        if (pos + 1 >= outLen) {
            out[outLen - 1] = '\0';
            return false;
        }
        out[pos++] = c;
    }

    out[outLen - 1] = '\0';
    return false;
}

bool appendText_(char* out, size_t outLen, size_t& pos, const char* text)
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

bool appendFormat_(char* out, size_t outLen, size_t& pos, const char* fmt, ...)
{
    if (!out || !fmt || outLen == 0 || pos >= outLen) return false;

    va_list args;
    va_start(args, fmt);
    const int wrote = vsnprintf(out + pos, outLen - pos, fmt, args);
    va_end(args);

    if (wrote < 0) return false;
    if ((size_t)wrote >= (outLen - pos)) {
        out[outLen - 1] = '\0';
        return false;
    }
    pos += (size_t)wrote;
    return true;
}

size_t jsonEscapedLen_(const char* text)
{
    size_t len = 2;  // quotes
    if (!text) return len;

    for (const char* p = text; *p != '\0'; ++p) {
        switch (*p) {
        case '\\':
        case '"':
        case '\b':
        case '\f':
        case '\n':
        case '\r':
        case '\t':
            len += 2;
            break;
        default:
            len += ((unsigned char)*p < 0x20U) ? 1U : 1U;
            break;
        }
    }
    return len;
}

bool appendEscapedJsonString_(char* out, size_t outLen, size_t& pos, const char* text)
{
    if (!appendText_(out, outLen, pos, "\"")) return false;
    if (text) {
        for (const char* p = text; *p != '\0'; ++p) {
            const char c = *p;
            switch (c) {
            case '\\':
                if (!appendText_(out, outLen, pos, "\\\\")) return false;
                break;
            case '"':
                if (!appendText_(out, outLen, pos, "\\\"")) return false;
                break;
            case '\b':
                if (!appendText_(out, outLen, pos, "\\b")) return false;
                break;
            case '\f':
                if (!appendText_(out, outLen, pos, "\\f")) return false;
                break;
            case '\n':
                if (!appendText_(out, outLen, pos, "\\n")) return false;
                break;
            case '\r':
                if (!appendText_(out, outLen, pos, "\\r")) return false;
                break;
            case '\t':
                if (!appendText_(out, outLen, pos, "\\t")) return false;
                break;
            default: {
                const char safe[2] = {((unsigned char)c < 0x20U) ? '?' : c, '\0'};
                if (!appendText_(out, outLen, pos, safe)) return false;
                break;
            }
            }
        }
    }
    return appendText_(out, outLen, pos, "\"");
}

bool appendJsonFloatOrNull_(char* out, size_t outLen, size_t& pos, bool valid, float value, uint8_t decimals)
{
    if (!valid) return appendText_(out, outLen, pos, "null");
    return appendFormat_(out, outLen, pos, "%.*f", (int)decimals, (double)value);
}

bool appendJsonBoolOrNull_(char* out, size_t outLen, size_t& pos, bool valid, bool value)
{
    if (!valid) return appendText_(out, outLen, pos, "null");
    return appendText_(out, outLen, pos, value ? "true" : "false");
}

}  // namespace

const ModuleTaskSpec* I2CCfgServerModule::taskSpecs() const
{
    static ModuleTaskSpec spec;
    if (!started_) return nullptr;
    spec = {
        "I2CfgAct",
        2560,
        1,
        0,
        &I2CCfgServerModule::actionTaskStatic_,
        const_cast<I2CCfgServerModule*>(this)
    };
    return &spec;
}

bool I2CCfgServerModule::registerRuntimeUiProvider(const IRuntimeUiValueProvider* provider)
{
    return runtimeUiRegistry_.registerProvider(provider);
}

void I2CCfgServerModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::I2cCfg;
    constexpr uint8_t kCfgBranchId = kI2cServerCfgBranch;

    cfg.registerVar(enabledVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sdaVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(sclVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(freqVar_, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(addrVar_, kCfgModuleId, kCfgBranchId);

    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    const DataStoreService* dsSvc = services.get<DataStoreService>(ServiceId::DataStore);
    dataStore_ = dsSvc ? dsSvc->store : nullptr;
    cfgStore_ = &cfg;
    (void)logHub_;

    resetPatchState_();
    LOGI("I2C cfg server config registered");
}

void I2CCfgServerModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               kI2cServerCfgProducerId,
                               kI2cServerCfgRoutes,
                               (uint8_t)(sizeof(kI2cServerCfgRoutes) / sizeof(kI2cServerCfgRoutes[0])),
                               services);
    }
}

void I2CCfgServerModule::onStart(ConfigStore&, ServiceRegistry&)
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
    int32_t sda = cfgData_.sda;
    int32_t scl = cfgData_.scl;

    if (!link_.beginSlave(kInterlinkBus,
                          cfgData_.address,
                          sda,
                          scl,
                          (uint32_t)(cfgData_.freqHz <= 0 ? 100000 : cfgData_.freqHz))) {
        LOGE("I2C cfg server start failed");
        return;
    }
    link_.setSlaveCallbacks(onReceiveStatic_, onRequestStatic_, this);
    started_ = true;
    LOGI("I2C cfg server started app_role=server i2c_role=slave addr=0x%02X bus=%u sda=%ld scl=%ld freq=%ld",
         (unsigned)cfgData_.address,
         (unsigned)kInterlinkBus,
         (long)sda,
         (long)scl,
         (long)cfgData_.freqHz);
}

void I2CCfgServerModule::resetPatchState_()
{
    patchExpected_ = 0;
    patchWritten_ = 0;
    patchBuf_[0] = '\0';
}

bool I2CCfgServerModule::collectPoolModeFlags_(bool& hasModeOut,
                                               bool& autoModeOut,
                                               bool& winterModeOut,
                                               bool& phAutoModeOut,
                                               bool& orpAutoModeOut)
{
    hasModeOut = false;
    autoModeOut = false;
    winterModeOut = false;
    phAutoModeOut = false;
    orpAutoModeOut = false;
    memset(poolModeJsonScratch_, 0, sizeof(poolModeJsonScratch_));

    bool truncated = false;
    bool ok = false;
    if (cfgStore_) {
        ok = cfgStore_->toJsonModule("poollogic/mode",
                                     poolModeJsonScratch_,
                                     sizeof(poolModeJsonScratch_),
                                     &truncated,
                                     true);
    } else if (cfgSvc_ && cfgSvc_->toJsonModule) {
        ok = cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                   "poollogic/mode",
                                   poolModeJsonScratch_,
                                   sizeof(poolModeJsonScratch_),
                                   &truncated);
    }
    (void)truncated;
    if (!ok) return false;

    hasModeOut = true;
    (void)parseJsonBoolField_(poolModeJsonScratch_, "auto_mode", autoModeOut);
    (void)parseJsonBoolField_(poolModeJsonScratch_, "winter_mode", winterModeOut);
    (void)parseJsonBoolField_(poolModeJsonScratch_, "ph_auto_mode", phAutoModeOut);
    (void)parseJsonBoolField_(poolModeJsonScratch_, "orp_auto_mode", orpAutoModeOut);
    return true;
}

void I2CCfgServerModule::collectActiveAlarmCodes_(uint8_t& activeAlarmCountOut, uint8_t& activeAlarmCodeCountOut)
{
    activeAlarmCountOut = 0;
    activeAlarmCodeCountOut = 0;
    memset(activeAlarmCodes_, 0, sizeof(activeAlarmCodes_));

    uint16_t activeAlarmCodeSeen = 0;
    if (alarmSvc_ && alarmSvc_->activeCount) {
        activeAlarmCountOut = alarmSvc_->activeCount(alarmSvc_->ctx);
    }

    if (alarmSvc_ && alarmSvc_->buildSnapshot) {
        memset(alarmJsonScratch_, 0, sizeof(alarmJsonScratch_));
        if (alarmSvc_->buildSnapshot(alarmSvc_->ctx, alarmJsonScratch_, sizeof(alarmJsonScratch_))) {
            const char* p = alarmJsonScratch_;
            while ((p = strstr(p, "\"code\":\"")) != nullptr) {
                p += 8;

                char code[sizeof(activeAlarmCodes_[0])] = {0};
                const char* afterCode = nullptr;
                if (!extractJsonString_(p, code, sizeof(code), afterCode)) break;

                const char* activeValue = strstr(afterCode, "\"active\":");
                if (!activeValue) break;
                activeValue += strlen("\"active\":");
                while (*activeValue == ' ' || *activeValue == '\t' || *activeValue == '\r' || *activeValue == '\n') {
                    ++activeValue;
                }
                if (strncmp(activeValue, "true", 4) == 0) {
                    if (activeAlarmCodeSeen < kMaxAlarmCodes) {
                        snprintf(activeAlarmCodes_[activeAlarmCodeSeen],
                                 sizeof(activeAlarmCodes_[0]),
                                 "%s",
                                 code);
                    }
                    ++activeAlarmCodeSeen;
                }
                p = afterCode;
            }
        }
    }

    if (activeAlarmCodeSeen == 0U && alarmSvc_ && alarmSvc_->listIds && alarmSvc_->isActive) {
        AlarmId ids[Limits::Alarm::MaxAlarms]{};
        const uint8_t idCount = alarmSvc_->listIds(alarmSvc_->ctx, ids, (uint8_t)Limits::Alarm::MaxAlarms);
        for (uint8_t i = 0; i < idCount; ++i) {
            if (!alarmSvc_->isActive(alarmSvc_->ctx, ids[i])) continue;
            if (activeAlarmCodeSeen < kMaxAlarmCodes) {
                snprintf(activeAlarmCodes_[activeAlarmCodeSeen],
                         sizeof(activeAlarmCodes_[0]),
                         "alarm_%u",
                         (unsigned)((uint16_t)ids[i]));
            }
            ++activeAlarmCodeSeen;
        }
    }

    if (activeAlarmCountOut < activeAlarmCodeSeen) {
        activeAlarmCountOut = (activeAlarmCodeSeen > 255U) ? 255U : (uint8_t)activeAlarmCodeSeen;
    }
    activeAlarmCodeCountOut = (activeAlarmCodeSeen > kMaxAlarmCodes) ? kMaxAlarmCodes : (uint8_t)activeAlarmCodeSeen;
}

bool I2CCfgServerModule::isValidStatusDomain_(FlowStatusDomain domain)
{
    switch (domain) {
    case FlowStatusDomain::System:
    case FlowStatusDomain::Wifi:
    case FlowStatusDomain::Mqtt:
    case FlowStatusDomain::I2c:
    case FlowStatusDomain::Pool:
    case FlowStatusDomain::Alarm:
        return true;
    default:
        return false;
    }
}

bool I2CCfgServerModule::buildRuntimeStatusDomainJson_(FlowStatusDomain domain, bool& truncatedOut)
{
    switch (domain) {
    case FlowStatusDomain::System: return buildRuntimeStatusSystemJson_(truncatedOut);
    case FlowStatusDomain::Wifi: return buildRuntimeStatusWifiJson_(truncatedOut);
    case FlowStatusDomain::Mqtt: return buildRuntimeStatusMqttJson_(truncatedOut);
    case FlowStatusDomain::I2c: return buildRuntimeStatusI2cJson_(truncatedOut);
    case FlowStatusDomain::Pool: return buildRuntimeStatusPoolJson_(truncatedOut);
    case FlowStatusDomain::Alarm: return buildRuntimeStatusAlarmJson_(truncatedOut);
    default:
        truncatedOut = false;
        return false;
    }
}

bool I2CCfgServerModule::buildRuntimeStatusSystemJson_(bool& truncatedOut)
{
    truncatedOut = false;
    SystemStatsSnapshot snap{};
    SystemStats::collect(snap);

    size_t pos = 0;
    statusJson_[0] = '\0';
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, "{\"ok\":true,\"fw\":")) return false;
    if (!appendEscapedJsonString_(statusJson_, sizeof(statusJson_), pos, FirmwareVersion::Full)) return false;
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       ",\"upms\":%llu",
                       (unsigned long long)snap.uptimeMs64)) return false;
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       ",\"heap\":{\"free\":%u,\"min_free\":%u,\"larg\":%u,\"frag\":%u}}",
                       (unsigned)snap.heap.freeBytes,
                       (unsigned)snap.heap.minFreeBytes,
                       (unsigned)snap.heap.largestFreeBlock,
                       (unsigned)snap.heap.fragPercent)) return false;
    return true;
}

bool I2CCfgServerModule::buildRuntimeStatusWifiJson_(bool& truncatedOut)
{
    truncatedOut = false;

    const bool wifiUp = dataStore_ ? wifiReady(*dataStore_) : false;
    const IpV4 ip = dataStore_ ? wifiIp(*dataStore_) : IpV4{0, 0, 0, 0};

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

    size_t pos = 0;
    statusJson_[0] = '\0';
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       "{\"ok\":true,\"wifi\":{\"rdy\":%s,\"ip\":",
                       wifiUp ? "true" : "false")) return false;
    if (!appendEscapedJsonString_(statusJson_, sizeof(statusJson_), pos, ipTxt)) return false;
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       ",\"rssi\":%ld,\"hrss\":%s}}",
                       (long)rssi,
                       hasRssi ? "true" : "false")) return false;
    return true;
}

bool I2CCfgServerModule::buildRuntimeStatusMqttJson_(bool& truncatedOut)
{
    truncatedOut = false;

    const bool mqttUp = dataStore_ ? mqttReady(*dataStore_) : false;
    const uint32_t mqttRxDropCnt = dataStore_ ? mqttRxDrop(*dataStore_) : 0;
    const uint32_t mqttParseFailCnt = dataStore_ ? mqttParseFail(*dataStore_) : 0;
    const uint32_t mqttHandlerFailCnt = dataStore_ ? mqttHandlerFail(*dataStore_) : 0;
    const uint32_t mqttOversizeDropCnt = dataStore_ ? mqttOversizeDrop(*dataStore_) : 0;
    char mqttServer[96] = {0};

    if (cfgStore_) {
        char mqttCfgJson[320] = {0};
        bool truncated = false;
        cfgStore_->toJsonModule("mqtt", mqttCfgJson, sizeof(mqttCfgJson), &truncated, true);

        char mqttHost[Limits::Mqtt::Buffers::Host] = {0};
        int32_t mqttPort = 0;
        const char* hostValue = nullptr;
        if (findJsonValue_(mqttCfgJson, "host", hostValue) &&
            *hostValue == '"' &&
            extractJsonString_(hostValue + 1, mqttHost, sizeof(mqttHost), hostValue) &&
            mqttHost[0] != '\0') {
            (void)parseJsonIntField_(mqttCfgJson, "port", mqttPort);
            if (mqttPort > 0) {
                (void)snprintf(mqttServer, sizeof(mqttServer), "%s:%ld", mqttHost, (long)mqttPort);
            } else {
                (void)snprintf(mqttServer, sizeof(mqttServer), "%s", mqttHost);
            }
        }
    }

    size_t pos = 0;
    statusJson_[0] = '\0';
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       "{\"ok\":true,\"mqtt\":{\"rdy\":%s,\"srv\":",
                       mqttUp ? "true" : "false")) return false;
    if (!appendEscapedJsonString_(statusJson_, sizeof(statusJson_), pos, mqttServer)) return false;
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       ",\"rxdrp\":%lu,\"prsf\":%lu,\"hndf\":%lu,\"ovr\":%lu}}",
                       (unsigned long)mqttRxDropCnt,
                       (unsigned long)mqttParseFailCnt,
                       (unsigned long)mqttHandlerFailCnt,
                       (unsigned long)mqttOversizeDropCnt)) return false;
    return true;
}

bool I2CCfgServerModule::buildRuntimeStatusI2cJson_(bool& truncatedOut)
{
    truncatedOut = false;

    const uint32_t nowMs = millis();
    const bool hasSupervisorSeen = reqCount_ > 0;
    const uint32_t lastReqAgoMs = hasSupervisorSeen ? (nowMs - lastReqMs_) : 0U;
    const bool supervisorLinkOk = started_ && hasSupervisorSeen && (lastReqAgoMs <= 15000U);

    size_t pos = 0;
    statusJson_[0] = '\0';
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       "{\"ok\":true,\"i2c\":{\"ena\":%s,\"sta\":%s,\"adr\":%u,\"req\":%lu,\"breq\":%lu,\"seen\":%s,\"ago\":%lu,\"lnk\":%s}}",
                       cfgData_.enabled ? "true" : "false",
                       started_ ? "true" : "false",
                       (unsigned)cfgData_.address,
                       (unsigned long)reqCount_,
                       (unsigned long)badReqCount_,
                       hasSupervisorSeen ? "true" : "false",
                       (unsigned long)lastReqAgoMs,
                       supervisorLinkOk ? "true" : "false")) return false;
    return true;
}

bool I2CCfgServerModule::buildRuntimeStatusPoolJson_(bool& truncatedOut)
{
    truncatedOut = false;

    bool poolHasMode = false;
    bool poolAutoMode = false;
    bool poolWinterMode = false;
    bool poolPhAutoMode = false;
    bool poolOrpAutoMode = false;
    float waterTemp = 0.0f;
    float airTemp = 0.0f;
    float phValue = 0.0f;
    float orpValue = 0.0f;
    bool filtrationOn = false;
    bool chlorinePumpOn = false;
    bool phPumpOn = false;
    bool robotOn = false;
    (void)collectPoolModeFlags_(poolHasMode, poolAutoMode, poolWinterMode, poolPhAutoMode, poolOrpAutoMode);

    const bool haveWaterTemp =
        dataStore_ && ioEndpointFloat(*dataStore_, PoolBinding::kSensorBindings[PoolBinding::kSensorSlotWaterTemp].runtimeIndex, waterTemp);
    const bool haveAirTemp =
        dataStore_ && ioEndpointFloat(*dataStore_, PoolBinding::kSensorBindings[PoolBinding::kSensorSlotAirTemp].runtimeIndex, airTemp);
    const bool havePh =
        dataStore_ && ioEndpointFloat(*dataStore_, PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPh].runtimeIndex, phValue);
    const bool haveOrp =
        dataStore_ && ioEndpointFloat(*dataStore_, PoolBinding::kSensorBindings[PoolBinding::kSensorSlotOrp].runtimeIndex, orpValue);

    PoolDeviceRuntimeStateEntry deviceState{};
    const bool haveFiltration =
        dataStore_ && poolDeviceRuntimeState(*dataStore_, PoolBinding::kDeviceSlotFiltrationPump, deviceState);
    if (haveFiltration) filtrationOn = deviceState.actualOn;
    const bool haveChlorinePump =
        dataStore_ && poolDeviceRuntimeState(*dataStore_, PoolBinding::kDeviceSlotChlorinePump, deviceState);
    if (haveChlorinePump) chlorinePumpOn = deviceState.actualOn;
    const bool havePhPump =
        dataStore_ && poolDeviceRuntimeState(*dataStore_, PoolBinding::kDeviceSlotPhPump, deviceState);
    if (havePhPump) phPumpOn = deviceState.actualOn;
    const bool haveRobot =
        dataStore_ && poolDeviceRuntimeState(*dataStore_, PoolBinding::kDeviceSlotRobot, deviceState);
    if (haveRobot) robotOn = deviceState.actualOn;

    size_t pos = 0;
    statusJson_[0] = '\0';
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       "{\"ok\":true,\"pool\":{\"has\":%s,\"auto\":%s,\"wint\":%s,\"pha\":%s,\"ora\":%s,\"wat\":",
                       poolHasMode ? "true" : "false",
                       poolAutoMode ? "true" : "false",
                       poolWinterMode ? "true" : "false",
                       poolPhAutoMode ? "true" : "false",
                       poolOrpAutoMode ? "true" : "false")) return false;
    if (!appendJsonFloatOrNull_(statusJson_, sizeof(statusJson_), pos, haveWaterTemp, waterTemp, 1)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"air\":")) return false;
    if (!appendJsonFloatOrNull_(statusJson_, sizeof(statusJson_), pos, haveAirTemp, airTemp, 1)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"ph\":")) return false;
    if (!appendJsonFloatOrNull_(statusJson_, sizeof(statusJson_), pos, havePh, phValue, 2)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"orp\":")) return false;
    if (!appendJsonFloatOrNull_(statusJson_, sizeof(statusJson_), pos, haveOrp, orpValue, 0)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"fil\":")) return false;
    if (!appendJsonBoolOrNull_(statusJson_, sizeof(statusJson_), pos, haveFiltration, filtrationOn)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"php\":")) return false;
    if (!appendJsonBoolOrNull_(statusJson_, sizeof(statusJson_), pos, havePhPump, phPumpOn)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"clp\":")) return false;
    if (!appendJsonBoolOrNull_(statusJson_, sizeof(statusJson_), pos, haveChlorinePump, chlorinePumpOn)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, ",\"rbt\":")) return false;
    if (!appendJsonBoolOrNull_(statusJson_, sizeof(statusJson_), pos, haveRobot, robotOn)) return false;
    if (!appendText_(statusJson_, sizeof(statusJson_), pos, "}}")) return false;
    return true;
}

bool I2CCfgServerModule::buildRuntimeStatusAlarmJson_(bool& truncatedOut)
{
    truncatedOut = false;

    uint8_t activeAlarmCount = 0;
    uint8_t activeAlarmCodeCount = 0;
    collectActiveAlarmCodes_(activeAlarmCount, activeAlarmCodeCount);

    size_t pos = 0;
    statusJson_[0] = '\0';
    if (!appendFormat_(statusJson_,
                       sizeof(statusJson_),
                       pos,
                       "{\"ok\":true,\"alm\":{\"cnt\":%u,\"codes\":[",
                       (unsigned)activeAlarmCount)) return false;

    for (uint8_t i = 0; i < activeAlarmCodeCount; ++i) {
        const size_t reserve = 4U;
        const size_t needed = (i > 0 ? 1U : 0U) + jsonEscapedLen_(activeAlarmCodes_[i]) + reserve;
        if ((pos + needed) >= sizeof(statusJson_)) {
            truncatedOut = true;
            break;
        }
        if (i > 0 && !appendText_(statusJson_, sizeof(statusJson_), pos, ",")) return false;
        if (!appendEscapedJsonString_(statusJson_, sizeof(statusJson_), pos, activeAlarmCodes_[i])) return false;
    }

    return appendText_(statusJson_, sizeof(statusJson_), pos, "]}}");
}

bool I2CCfgServerModule::writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const
{
    const RuntimeUiId runtimeId = makeRuntimeUiId(moduleId(), valueId);
    const bool hasSupervisorSeen = reqCount_ > 0U;
    const uint32_t nowMs = millis();
    const uint32_t lastReqAgoMs = hasSupervisorSeen ? (nowMs - lastReqMs_) : 0U;
    const bool supervisorLinkOk = started_ && hasSupervisorSeen && (lastReqAgoMs <= 15000U);

    switch (valueId) {
        case kI2cRuntimeUiValueLinkOk:
            return writer.writeBool(runtimeId, supervisorLinkOk);
        case kI2cRuntimeUiValueSeen:
            return writer.writeBool(runtimeId, hasSupervisorSeen);
        case kI2cRuntimeUiValueReqCount:
            return writer.writeU32(runtimeId, reqCount_);
        case kI2cRuntimeUiValueBadReqCount:
            return writer.writeU32(runtimeId, badReqCount_);
        case kI2cRuntimeUiValueLastReqAgoMs:
            return hasSupervisorSeen
                ? writer.writeU32(runtimeId, lastReqAgoMs)
                : writer.writeUnavailable(runtimeId);
        default:
            return false;
    }
}

bool I2CCfgServerModule::buildRuntimeAlarmSnapshotJson_(bool& truncatedOut)
{
    truncatedOut = false;
    memset(alarmJsonScratch_, 0, sizeof(alarmJsonScratch_));
    if (!alarmSvc_ || !alarmSvc_->buildSnapshot) return false;
    if (!alarmSvc_->buildSnapshot(alarmSvc_->ctx, alarmJsonScratch_, sizeof(alarmJsonScratch_))) return false;

    const size_t n = strnlen(alarmJsonScratch_, sizeof(alarmJsonScratch_));
    if (n == 0U || n >= sizeof(alarmJsonScratch_)) {
        truncatedOut = true;
        return false;
    }
    return true;
}

void I2CCfgServerModule::queueSystemAction_(PendingSystemAction action)
{
    if (action == PendingSystemAction::None) return;
    portENTER_CRITICAL(&actionMux_);
    pendingAction_ = (uint8_t)action;
    portEXIT_CRITICAL(&actionMux_);
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
    if (payload &&
        payloadLen > 0 &&
        payload != (txFrame_ + I2cCfgProtocol::RespHeaderSize)) {
        memcpy(txFrame_ + I2cCfgProtocol::RespHeaderSize, payload, payloadLen);
    }
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
    if (op != I2cCfgProtocol::OpGetRuntimeAlarmBegin && op != I2cCfgProtocol::OpGetRuntimeAlarmChunk) {
        alarmSnapshotLen_ = 0;
        alarmSnapshotValid_ = false;
        alarmSnapshotTruncated_ = false;
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
        const char* modules[Limits::Config::Capacity::ModuleListMax] = {0};
        const uint8_t count = cfgSvc_->listModules
                                  ? cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Config::Capacity::ModuleListMax)
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
        const char* modules[Limits::Config::Capacity::ModuleListMax] = {0};
        const uint8_t count = cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Config::Capacity::ModuleListMax);
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

        const char* modules[Limits::Config::Capacity::ModuleListMax] = {0};
        const uint8_t countModules =
            cfgSvc_->listModules ? cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Config::Capacity::ModuleListMax) : 0;

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

        const char* modules[Limits::Config::Capacity::ModuleListMax] = {0};
        const uint8_t countModules =
            cfgSvc_->listModules(cfgSvc_->ctx, modules, Limits::Config::Capacity::ModuleListMax);

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
        if (payloadLen < 1) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        const FlowStatusDomain domain = static_cast<FlowStatusDomain>(payload[0]);
        if (!isValidStatusDomain_(domain)) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }
        bool truncated = false;
        if (!buildRuntimeStatusDomainJson_(domain, truncated)) {
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

    if (op == I2cCfgProtocol::OpGetRuntimeAlarmBegin) {
        alarmSnapshotLen_ = 0;
        alarmSnapshotValid_ = false;
        alarmSnapshotTruncated_ = false;
        buildResponse_(op, seq, I2cCfgProtocol::StatusNotReady, nullptr, 0);
        return;
    }

    if (op == I2cCfgProtocol::OpGetRuntimeAlarmChunk) {
        alarmSnapshotLen_ = 0;
        alarmSnapshotValid_ = false;
        alarmSnapshotTruncated_ = false;
        buildResponse_(op, seq, I2cCfgProtocol::StatusNotReady, nullptr, 0);
        return;
    }

    if (op == I2cCfgProtocol::OpGetRuntimeUiValues) {
        if (payloadLen < 1U) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }

        const uint8_t count = payload[0];
        if (count == 0U) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusOk, nullptr, 0);
            return;
        }
        if (count > kRuntimeUiMaxIdsPerRequest || payloadLen != (size_t)(1U + ((size_t)count * 2U))) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusBadRequest, nullptr, 0);
            return;
        }

        RuntimeUiId ids[kRuntimeUiMaxIdsPerRequest] = {};
        for (uint8_t i = 0; i < count; ++i) {
            const size_t offset = 1U + ((size_t)i * 2U);
            ids[i] = (RuntimeUiId)((RuntimeUiId)payload[offset] |
                                   ((RuntimeUiId)payload[offset + 1U] << 8));
        }

        size_t respLen = 0U;
        uint8_t* respPayload = txFrame_ + I2cCfgProtocol::RespHeaderSize;
        if (!runtimeUiSvc_.readValues(ids, count, respPayload, I2cCfgProtocol::MaxPayload, respLen)) {
            buildResponse_(op, seq, I2cCfgProtocol::StatusOverflow, nullptr, 0);
            return;
        }

        buildResponse_(op, seq, I2cCfgProtocol::StatusOk, respPayload, respLen);
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
            (void)writeOkJson(ack, sizeof(ack), "elink/apply");
        } else {
            (void)writeErrorJson(ack, sizeof(ack), ErrorCode::CfgApplyFailed, "elink/apply");
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
