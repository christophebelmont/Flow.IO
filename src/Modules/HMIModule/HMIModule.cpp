/**
 * @file HMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/HMIModule/HMIModule.h"

#include "Board/BoardSerialMap.h"
#include "Core/ConfigStore.h"
#include "Core/EventBus/EventPayloads.h"
#include "Core/AlarmIds.h"
#include "Domain/Pool/PoolBindings.h"
#include "Modules/IOModule/IORuntime.h"
#include "Modules/Network/MQTTModule/MQTTRuntime.h"
#include "Modules/Network/TimeModule/TimeRuntime.h"
#include "Modules/Network/WifiModule/WifiRuntime.h"
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"
#include "Modules/PoolDeviceModule/PoolDeviceRuntime.h"

namespace {

#ifndef FLOW_HMI_CONFIG_MENU_ENABLED
#define FLOW_HMI_CONFIG_MENU_ENABLED 1
#endif
static constexpr bool kConfigMenuEnabled = (FLOW_HMI_CONFIG_MENU_ENABLED != 0);
static constexpr const char* kHmiModulePrefix = "hmi/";
static constexpr const char* kPoolLogicSensorsModule = "poollogic/sensors";
static constexpr const char* kPoolLogicDeviceModule = "poollogic/device";
static constexpr const char* kPoolLogicModeModule = "poollogic/mode";
static constexpr const char* kPoolLogicPidModule = "poollogic/pid";
static constexpr size_t kPoolLogicSensorsJsonBufSize = 320U;
static constexpr size_t kPoolLogicDeviceJsonBufSize = 192U;
static constexpr size_t kPoolLogicModeJsonBufSize = 256U;
static constexpr size_t kPoolLogicPidJsonBufSize = 512U;
static constexpr uint8_t kLedBitMqttConnected = 0;
static constexpr uint8_t kLedBitPageSelect = 1;
static constexpr uint8_t kLedBitModeA = 2;
static constexpr uint8_t kLedBitModeB = 3;
static constexpr uint8_t kLedBitAlarmA = 4;
static constexpr uint8_t kLedBitAlarmB = 5;
static constexpr uint8_t kLedBitAlarmC = 6;
static constexpr uint8_t kLedBitAlarmD = 7;
static constexpr uint32_t kLedPageTogglePeriodMs = 2000U;
static constexpr uint32_t kWifiBlinkPeriodMs = 125U;
static constexpr uint8_t kInvalidRuntimeIndex = 0xFFU;
static constexpr uint32_t kHomePublishWaterTemp = 1UL << 0;
static constexpr uint32_t kHomePublishAirTemp = 1UL << 1;
static constexpr uint32_t kHomePublishPh = 1UL << 2;
static constexpr uint32_t kHomePublishOrp = 1UL << 3;
static constexpr uint32_t kHomePublishPhGauge = 1UL << 4;
static constexpr uint32_t kHomePublishOrpGauge = 1UL << 5;
static constexpr uint32_t kHomePublishStateBits = 1UL << 6;
static constexpr uint32_t kHomePublishTime = 1UL << 7;
static constexpr uint32_t kHomePublishDate = 1UL << 8;
static constexpr uint32_t kHomePublishAlarmBits = 1UL << 9;
static constexpr uint32_t kHomePublishErrorMessage = 1UL << 10;
static constexpr uint32_t kHomePublishAll = kHomePublishWaterTemp |
                                            kHomePublishAirTemp |
                                            kHomePublishPh |
                                            kHomePublishOrp |
                                            kHomePublishPhGauge |
                                            kHomePublishOrpGauge |
                                            kHomePublishStateBits |
                                            kHomePublishTime |
                                            kHomePublishDate |
                                            kHomePublishAlarmBits |
                                            kHomePublishErrorMessage;
static constexpr uint32_t kClockPublishCheckMs = 1000U;
static constexpr uint32_t kInvalidClockStamp = 0xFFFFFFFFUL;
static constexpr uint32_t kRtcFallbackDelayMs = 30000U;
static constexpr uint32_t kRtcFallbackRetryMs = 10000U;
static constexpr uint32_t kRtcPushRetryMs = 60000U;
static constexpr uint16_t kNextionRtcReadTimeoutMs = 180U;
static constexpr uint32_t kNextionDisplayVersionCurrent = 1U;

static const char* const kMonthNamesFr[] = {
    "Janvier",
    "F\xE9""vrier",
    "Mars",
    "Avril",
    "Mai",
    "Juin",
    "Juillet",
    "Ao\xFB""t",
    "Septembre",
    "Octobre",
    "Novembre",
    "D\xE9""cembre",
};

struct PoolLogicModeFlags {
    bool autoMode = false;
    bool winterMode = false;
    bool phAutoMode = false;
    bool orpAutoMode = false;
};

static uint16_t gaugePercentFromSetpoint_(float value, float setpoint)
{
    const float span = fabsf(setpoint) * 0.30f;
    if (span <= 0.0001f) return 140U;

    const float low = setpoint - span;
    const float high = setpoint + span;
    const float normalized = ((value - low) / (high - low)) * 280.0f;
    if (normalized <= 0.0f) return 0U;
    if (normalized >= 280.0f) return 280U;
    return (uint16_t)lroundf(normalized);
}

static inline uint8_t normalizeLedPage_(uint8_t page)
{
    return (page == 2U) ? 2U : 1U;
}

static bool isRtcDateTimeValid_(const HmiRtcDateTime& rtc)
{
    return rtc.year >= 2021U &&
           rtc.year <= 2099U &&
           rtc.month >= 1U &&
           rtc.month <= 12U &&
           rtc.day >= 1U &&
           rtc.day <= 31U &&
           rtc.hour <= 23U &&
           rtc.minute <= 59U &&
           rtc.second <= 59U;
}

static bool rtcDateTimeToEpoch_(const HmiRtcDateTime& rtc, uint64_t& epoch)
{
    epoch = 0ULL;
    if (!isRtcDateTimeValid_(rtc)) return false;

    struct tm local{};
    local.tm_year = (int)rtc.year - 1900;
    local.tm_mon = (int)rtc.month - 1;
    local.tm_mday = (int)rtc.day;
    local.tm_hour = (int)rtc.hour;
    local.tm_min = (int)rtc.minute;
    local.tm_sec = (int)rtc.second;
    local.tm_isdst = -1;

    const time_t converted = mktime(&local);
    if (converted < (time_t)1609459200) return false;
    if ((uint16_t)(local.tm_year + 1900) != rtc.year ||
        (uint8_t)(local.tm_mon + 1) != rtc.month ||
        (uint8_t)local.tm_mday != rtc.day ||
        (uint8_t)local.tm_hour != rtc.hour ||
        (uint8_t)local.tm_min != rtc.minute) {
        return false;
    }

    epoch = (uint64_t)converted;
    return true;
}

static bool epochToRtcDateTime_(uint64_t epoch, HmiRtcDateTime& out)
{
    out = HmiRtcDateTime{};
    const time_t t = (time_t)epoch;
    struct tm local{};
    if (!localtime_r(&t, &local)) return false;
    out.year = (uint16_t)(local.tm_year + 1900);
    out.month = (uint8_t)(local.tm_mon + 1);
    out.day = (uint8_t)local.tm_mday;
    out.hour = (uint8_t)local.tm_hour;
    out.minute = (uint8_t)local.tm_min;
    out.second = (uint8_t)local.tm_sec;
    return isRtcDateTimeValid_(out);
}

static uint32_t rtcDayStamp_(uint64_t epoch)
{
    const time_t t = (time_t)epoch;
    struct tm local{};
    if (!localtime_r(&t, &local)) return kInvalidClockStamp;
    return ((uint32_t)(local.tm_year + 1900) * 1000UL) + (uint32_t)local.tm_yday;
}

static bool findJsonBool_(const char* json, const char* key, bool& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (strncmp(p, "true", 4) == 0) {
        out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        out = false;
        return true;
    }
    return false;
}

static bool findJsonUInt16_(const char* json, const char* key, uint16_t& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p < '0' || *p > '9') return false;

    unsigned long v = 0UL;
    while (*p >= '0' && *p <= '9') {
        v = (v * 10UL) + (unsigned long)(*p - '0');
        if (v > 65535UL) return false;
        ++p;
    }
    out = (uint16_t)v;
    return true;
}

static bool findJsonFloat_(const char* json, const char* key, float& out)
{
    if (!json || !key || key[0] == '\0') return false;
    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;

    char* end = nullptr;
    const float value = strtof(p, &end);
    if (end == p) return false;
    out = value;
    return true;
}

static bool extractJsonStringField_(const char* json, const char* key, char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;
    out[0] = '\0';
    if (!json || !key || key[0] == '\0') return false;

    char needle[48]{};
    const int nn = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    if (nn <= 0 || (size_t)nn >= sizeof(needle)) return false;
    const char* p = strstr(json, needle);
    if (!p) return false;
    p += nn;

    size_t pos = 0;
    while (*p != '\0' && *p != '"' && pos + 1 < outLen) {
        const unsigned char uc = (unsigned char)*p;
        if (uc < 32U) break;
        out[pos++] = (char)uc;
        ++p;
    }
    out[pos] = '\0';
    return pos > 0;
}

#if FLOW_HMI_CONFIG_MENU_ENABLED
static const ConfigMenuHint kHints[] = {
    {"poollogic/filtration", "filtr_start_min", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic/filtration", "filtr_stop_max", {ConfigMenuWidget::Slider, true, 0.0f, 23.0f, 1.0f, nullptr}},
    {"poollogic/pid", "ph_setpoint", {ConfigMenuWidget::Slider, true, 6.6f, 7.8f, 0.1f, nullptr}},
    {"poollogic/pid", "orp_setpoint", {ConfigMenuWidget::Slider, true, 450.0f, 950.0f, 10.0f, nullptr}},
    {"time", "tz", {ConfigMenuWidget::Select, true, 0.0f, 0.0f, 1.0f,
                    "CET-1CEST,M3.5.0/2,M10.5.0/3|UTC0|EST5EDT,M3.2.0/2,M11.1.0/2"}}
};
#endif

} // namespace

void HMIModule::applyOutputConfig_()
{
    IHmiDriver* wantedDriver =
        (cfgData_.nextionEnabled && !nextionDisabledByVersion_) ? static_cast<IHmiDriver*>(&nextion_) : nullptr;
    if (driver_ != wantedDriver) {
        driver_ = wantedDriver;
        driverReady_ = false;
        if (!driver_) configMenuActive_ = false;
        if (driver_) viewDirty_ = true;
    }

    TfaVeniceRf433Config veniceCfg{};
    veniceCfg.enabled = cfgData_.veniceEnabled;
    veniceCfg.txPin =
        (cfgData_.veniceTxGpio >= 0 && cfgData_.veniceTxGpio <= 127) ? (int8_t)cfgData_.veniceTxGpio : (int8_t)-1;
    venice_.setConfig(veniceCfg);

    if (!cfgData_.ledsEnabled) {
        wifiBlinkOn_ = false;
    }
    ledMaskValid_ = false;
}

bool HMIModule::requestRefresh_()
{
    if (configMenuActive_) viewDirty_ = true;
    return true;
}

bool HMIModule::openConfigHome_()
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    if (!ensureConfigMenuReady_()) return false;
    const bool ok = menu_.home();
    if (ok) {
        configMenuActive_ = true;
        viewDirty_ = true;
    }
    return ok;
#else
    return false;
#endif
}

bool HMIModule::openConfigModule_(const char* module)
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    if (!ensureConfigMenuReady_()) return false;
    const bool ok = menu_.openModule(module);
    if (ok) {
        configMenuActive_ = true;
        viewDirty_ = true;
    }
    return ok;
#else
    (void)module;
    return false;
#endif
}

bool HMIModule::setLedPage_(uint8_t page)
{
    const uint8_t newPage = normalizeLedPage_(page);
    if (ledPage_ != newPage) {
        ledPage_ = newPage;
        applyLedMask_(true);
    }
    return true;
}

uint8_t HMIModule::getLedPage_() const
{
    return ledPage_;
}

void HMIModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfg.registerVar(ledsEnabledVar_);
    cfg.registerVar(nextionEnabledVar_);
    cfg.registerVar(veniceEnabledVar_);
    cfg.registerVar(veniceTxGpioVar_);

    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    alarmSvc_ = services.get<AlarmService>(ServiceId::Alarm);
    ioSvc_ = services.get<IOServiceV2>(ServiceId::Io);
    cmdSvc_ = services.get<CommandService>(ServiceId::Command);
    timeSvc_ = services.get<TimeService>(ServiceId::Time);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    statusLedsSvc_ = services.get<StatusLedsService>(ServiceId::StatusLeds);
    auto* ebSvc = services.get<EventBusService>(ServiceId::EventBus);
    eventBus_ = ebSvc ? ebSvc->bus : nullptr;

    if (!cfgSvc_) {
        LOGE("Config service unavailable");
        return;
    }

#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        LOGI("Config menu enabled (lazy init)");
    }
#else
    {
        LOGI("Config menu disabled at compile-time");
    }
#endif

    if (eventBus_) {
        eventBus_->subscribe(EventId::ConfigChanged, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::DataChanged, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmRaised, &HMIModule::onEventStatic_, this);
        eventBus_->subscribe(EventId::AlarmCleared, &HMIModule::onEventStatic_, this);
    }

    if (!services.add(ServiceId::Hmi, &hmiSvc_)) {
        LOGE("service registration failed: %s", toString(ServiceId::Hmi));
    }

    NextionDriverConfig dcfg{};
    dcfg.serial = &Board::SerialMap::hmiSerial();
    dcfg.rxPin = Board::SerialMap::hmiRxPin();
    dcfg.txPin = Board::SerialMap::hmiTxPin();
    dcfg.baud = Board::SerialMap::HmiBaud;
    nextion_.setConfig(dcfg);
    applyOutputConfig_();
    driverReady_ = false;
    nextionDisabledByVersion_ = false;
    configMenuReady_ = false;
    configMenuActive_ = false;
    viewDirty_ = true;
    lastRenderMs_ = 0;
    ledPage_ = 1U;
    ledMaskValid_ = false;
    lastLedApplyTryMs_ = 0;
    lastLedPageToggleMs_ = millis();
    lastWifiBlinkToggleMs_ = millis();
    lastClockCheckMs_ = 0;
    lastClockMinuteStamp_ = kInvalidClockStamp;
    lastClockDayStamp_ = kInvalidClockStamp;
    lastRtcFallbackAttemptMs_ = 0;
    lastRtcPushAttemptMs_ = 0;
    lastRtcPushDayStamp_ = kInvalidClockStamp;
    rtcFallbackCompleted_ = false;
    nextionVersionDetected_ = false;
    nextionVersion_ = 0U;
    phIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPh].ioId;
    orpIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotOrp].ioId;
    airTempIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotAirTemp].ioId;
    poolLevelIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotPoolLevel].ioId;
    waterTempIoId_ = PoolBinding::kSensorBindings[PoolBinding::kSensorSlotWaterTemp].ioId;
    homeErrorMessage_[0] = '\0';

    LOGI("HMI service registered with driver=%s led_panel=%s",
         driver_ ? driver_->driverId() : "none",
         statusLedsSvc_ ? "on" : "off");
}

void HMIModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    applyOutputConfig_();
    refreshHomeBindings_();
    queueHomePublish_(kHomePublishAll);
    applyLedMask_(true);
}

void HMIModule::onEventStatic_(const Event& e, void* user)
{
    HMIModule* self = static_cast<HMIModule*>(user);
    if (self) self->onEvent_(e);
}

bool HMIModule::resolveIoRuntimeIndex_(IoId ioId, uint8_t& outIndex) const
{
    outIndex = kInvalidRuntimeIndex;

    if (ioId == IO_ID_INVALID) return false;

    if (const PoolSensorBinding* binding = PoolBinding::sensorBindingByIoId(ioId)) {
        outIndex = binding->runtimeIndex;
        return true;
    }

    if (!ioSvc_ || !ioSvc_->count || !ioSvc_->idAt) return false;

    const uint8_t count = ioSvc_->count(ioSvc_->ctx);
    for (uint8_t i = 0; i < count; ++i) {
        IoId curId = IO_ID_INVALID;
        if (ioSvc_->idAt(ioSvc_->ctx, i, &curId) != IO_OK) continue;
        if (curId != ioId) continue;
        outIndex = i;
        return true;
    }
    return false;
}

void HMIModule::refreshHomeBindings_()
{
    if (cfgSvc_ && cfgSvc_->toJsonModule) {
        char jsonBuf[kPoolLogicSensorsJsonBufSize]{};
        bool truncated = false;
        bool sensorsTruncated = false;
        bool foundPoolLevel = false;
        bool foundPh = false;
        bool foundOrp = false;
        bool foundWaterTemp = false;
        bool foundAirTemp = false;
        bool foundFiltrationSlot = false;
        bool foundPhPumpSlot = false;
        bool foundOrpPumpSlot = false;
        bool foundRobotSlot = false;
        bool foundFillingSlot = false;

        if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                  kPoolLogicSensorsModule,
                                  jsonBuf,
                                  sizeof(jsonBuf),
                                  &truncated)) {
            sensorsTruncated = truncated;
            uint16_t ioId = (uint16_t)poolLevelIoId_;
            foundPoolLevel = findJsonUInt16_(jsonBuf, "pool_lvl_io_id", ioId);
            if (foundPoolLevel) {
                poolLevelIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)phIoId_;
            foundPh = findJsonUInt16_(jsonBuf, "ph_io_id", ioId);
            if (foundPh) {
                phIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)orpIoId_;
            foundOrp = findJsonUInt16_(jsonBuf, "orp_io_id", ioId);
            if (foundOrp) {
                orpIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)waterTempIoId_;
            foundWaterTemp = findJsonUInt16_(jsonBuf, "wat_temp_io_id", ioId);
            if (foundWaterTemp) {
                waterTempIoId_ = (IoId)ioId;
            }
            ioId = (uint16_t)airTempIoId_;
            foundAirTemp = findJsonUInt16_(jsonBuf, "air_temp_io_id", ioId);
            if (foundAirTemp) {
                airTempIoId_ = (IoId)ioId;
            }
        } else {
            LOGW("HMI poollogic sensors export failed");
        }

        truncated = false;
        memset(jsonBuf, 0, sizeof(jsonBuf));
        if (cfgSvc_->toJsonModule(cfgSvc_->ctx,
                                  kPoolLogicDeviceModule,
                                  jsonBuf,
                                  kPoolLogicDeviceJsonBufSize,
                                  &truncated)) {
            uint16_t slot = filtrationDeviceSlot_;
            foundFiltrationSlot = findJsonUInt16_(jsonBuf, "filtr_slot", slot);
            if (foundFiltrationSlot) {
                filtrationDeviceSlot_ = (uint8_t)slot;
            }
            slot = phPumpDeviceSlot_;
            foundPhPumpSlot = findJsonUInt16_(jsonBuf, "ph_pump_slot", slot);
            if (foundPhPumpSlot) {
                phPumpDeviceSlot_ = (uint8_t)slot;
            }
            slot = orpPumpDeviceSlot_;
            foundOrpPumpSlot = findJsonUInt16_(jsonBuf, "orp_pump_slot", slot);
            if (foundOrpPumpSlot) {
                orpPumpDeviceSlot_ = (uint8_t)slot;
            }
            slot = robotDeviceSlot_;
            foundRobotSlot = findJsonUInt16_(jsonBuf, "robot_slot", slot);
            if (foundRobotSlot) {
                robotDeviceSlot_ = (uint8_t)slot;
            }
            slot = fillingDeviceSlot_;
            foundFillingSlot = findJsonUInt16_(jsonBuf, "fill_slot", slot);
            if (foundFillingSlot) {
                fillingDeviceSlot_ = (uint8_t)slot;
            }

            LOGD("HMI poollogic cfg sensors_trunc=%u device_trunc=%u keys lvl=%u ph=%u orp=%u wat=%u air=%u filtr=%u php=%u orpp=%u robot=%u fill=%u",
                 sensorsTruncated ? 1U : 0U,
                 truncated ? 1U : 0U,
                 foundPoolLevel ? 1U : 0U,
                 foundPh ? 1U : 0U,
                 foundOrp ? 1U : 0U,
                 foundWaterTemp ? 1U : 0U,
                 foundAirTemp ? 1U : 0U,
                 foundFiltrationSlot ? 1U : 0U,
                 foundPhPumpSlot ? 1U : 0U,
                 foundOrpPumpSlot ? 1U : 0U,
                 foundRobotSlot ? 1U : 0U,
                 foundFillingSlot ? 1U : 0U);
        } else {
            LOGW("HMI poollogic device export failed");
        }
    }

    (void)resolveIoRuntimeIndex_(phIoId_, phRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(orpIoId_, orpRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(waterTempIoId_, waterTempRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(airTempIoId_, airTempRuntimeIndex_);
    (void)resolveIoRuntimeIndex_(poolLevelIoId_, poolLevelRuntimeIndex_);

    LOGD("HMI home bindings ph=%u(rt=%u) orp=%u(rt=%u) wat=%u(rt=%u) air=%u(rt=%u) lvl=%u(rt=%u)",
         (unsigned)phIoId_,
         (unsigned)phRuntimeIndex_,
         (unsigned)orpIoId_,
         (unsigned)orpRuntimeIndex_,
         (unsigned)waterTempIoId_,
         (unsigned)waterTempRuntimeIndex_,
         (unsigned)airTempIoId_,
         (unsigned)airTempRuntimeIndex_,
         (unsigned)poolLevelIoId_,
         (unsigned)poolLevelRuntimeIndex_);
}

bool HMIModule::readPoolLogicModeFlags_(bool& autoMode,
                                        bool& winterMode,
                                        bool& phAutoMode,
                                        bool& orpAutoMode) const
{
    autoMode = false;
    winterMode = false;
    phAutoMode = false;
    orpAutoMode = false;
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;

    char jsonBuf[kPoolLogicModeJsonBufSize]{};
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx,
                               kPoolLogicModeModule,
                               jsonBuf,
                               sizeof(jsonBuf),
                               &truncated)) {
        return false;
    }

    bool v = false;
    if (findJsonBool_(jsonBuf, "auto_mode", v)) autoMode = v;
    if (findJsonBool_(jsonBuf, "winter_mode", v)) winterMode = v;
    if (findJsonBool_(jsonBuf, "ph_auto_mode", v)) phAutoMode = v;
    if (findJsonBool_(jsonBuf, "orp_auto_mode", v)) orpAutoMode = v;
    return true;
}

bool HMIModule::readPidSetpoints_(float& phSetpoint, float& orpSetpoint) const
{
    phSetpoint = 0.0f;
    orpSetpoint = 0.0f;
    if (!cfgSvc_ || !cfgSvc_->toJsonModule) return false;

    char jsonBuf[kPoolLogicPidJsonBufSize]{};
    bool truncated = false;
    if (!cfgSvc_->toJsonModule(cfgSvc_->ctx,
                               kPoolLogicPidModule,
                               jsonBuf,
                               sizeof(jsonBuf),
                               &truncated)) {
        return false;
    }

    bool found = false;
    float value = 0.0f;
    if (findJsonFloat_(jsonBuf, "ph_setpoint", value)) {
        phSetpoint = value;
        found = true;
    }
    if (findJsonFloat_(jsonBuf, "orp_setpoint", value)) {
        orpSetpoint = value;
        found = true;
    }
    return found;
}

bool HMIModule::readPoolDeviceActualOn_(uint8_t slot, bool& on) const
{
    on = false;
    if (!dsSvc_ || !dsSvc_->store) return false;
    PoolDeviceRuntimeStateEntry state{};
    if (!poolDeviceRuntimeState(*dsSvc_->store, slot, state)) return false;
    on = state.actualOn;
    return true;
}

bool HMIModule::isAlarmActive_(AlarmId id) const
{
    return alarmSvc_ && alarmSvc_->isActive && alarmSvc_->isActive(alarmSvc_->ctx, id);
}

bool HMIModule::isWaterLevelLow_() const
{
    if (isAlarmActive_(AlarmId::PoolWaterLevelLow)) return true;
    if (!ioSvc_ || !ioSvc_->readDigital) return false;
    if (poolLevelIoId_ == IO_ID_INVALID) return false;

    uint8_t levelOk = 0U;
    if (ioSvc_->readDigital(ioSvc_->ctx, poolLevelIoId_, &levelOk, nullptr, nullptr) != IO_OK) return false;
    return levelOk == 0U;
}

uint32_t HMIModule::buildHomeStateBits_() const
{
    uint32_t bits = 0U;
    PoolLogicModeFlags modes{};

    if (dsSvc_ && dsSvc_->store) {
        PoolDeviceRuntimeStateEntry state{};
        if (poolDeviceRuntimeState(*dsSvc_->store, filtrationDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_FILTER_PUMP_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, phPumpDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_PH_PUMP_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, orpPumpDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_ORP_PUMP_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, robotDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_ROBOT_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, lightsDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_LIGHTS_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, heaterDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_HEATER_ON);
        }
        if (poolDeviceRuntimeState(*dsSvc_->store, fillingDeviceSlot_, state) && state.actualOn) {
            bits |= (1UL << HMI_HOME_STATE_FILLING_ON);
        }
        if (wifiReady(*dsSvc_->store)) bits |= (1UL << HMI_HOME_STATE_WIFI_READY);
        if (mqttReady(*dsSvc_->store)) bits |= (1UL << HMI_HOME_STATE_MQTT_READY);
    }

    (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
    if (modes.autoMode) bits |= (1UL << HMI_HOME_STATE_AUTO_MODE);
    if (modes.phAutoMode) bits |= (1UL << HMI_HOME_STATE_PH_AUTO_MODE);
    if (modes.orpAutoMode) bits |= (1UL << HMI_HOME_STATE_ORP_AUTO_MODE);
    if (modes.winterMode) bits |= (1UL << HMI_HOME_STATE_WINTER_MODE);
    return bits;
}

uint32_t HMIModule::buildHomeAlarmBits_() const
{
    uint32_t bits = 0U;
    if (isWaterLevelLow_()) bits |= (1UL << HMI_HOME_ALARM_WATER_LEVEL_LOW);
    if (isAlarmActive_(AlarmId::PoolPhTankLow)) bits |= (1UL << HMI_HOME_ALARM_PH_TANK_LOW);
    if (isAlarmActive_(AlarmId::PoolChlorineTankLow)) bits |= (1UL << HMI_HOME_ALARM_CHLORINE_TANK_LOW);
    if (isAlarmActive_(AlarmId::PoolPhPumpMaxUptime)) bits |= (1UL << HMI_HOME_ALARM_PH_PUMP_RUNTIME);
    if (isAlarmActive_(AlarmId::PoolChlorinePumpMaxUptime)) bits |= (1UL << HMI_HOME_ALARM_ORP_PUMP_RUNTIME);
    if (isAlarmActive_(AlarmId::PoolPsiLow) || isAlarmActive_(AlarmId::PoolPsiHigh)) {
        bits |= (1UL << HMI_HOME_ALARM_PSI);
    }
    return bits;
}

bool HMIModule::publishHomeText_(HmiHomeTextField field)
{
    if (!driver_) return false;

    uint8_t runtimeIndex = kInvalidRuntimeIndex;
    char value[32]{};
    if (field == HmiHomeTextField::ErrorMessage) {
        return driver_->publishHomeText(field, homeErrorMessage_);
    }
    if (field == HmiHomeTextField::Time || field == HmiHomeTextField::Date) {
        struct tm local{};
        bool hasTime = timeSvc_ &&
                       timeSvc_->isSynced &&
                       timeSvc_->epoch &&
                       timeSvc_->isSynced(timeSvc_->ctx);
        if (hasTime) {
            const time_t epoch = (time_t)timeSvc_->epoch(timeSvc_->ctx);
            hasTime = localtime_r(&epoch, &local) != nullptr;
        }

        if (field == HmiHomeTextField::Time) {
            if (hasTime) snprintf(value, sizeof(value), "%02d:%02d", local.tm_hour, local.tm_min);
            else snprintf(value, sizeof(value), "--:--");
        } else {
            if (hasTime && local.tm_mon >= 0 && local.tm_mon < 12) {
                snprintf(value, sizeof(value), "%d %s", local.tm_mday, kMonthNamesFr[local.tm_mon]);
            } else {
                snprintf(value, sizeof(value), "-- ----");
            }
        }
        return driver_->publishHomeText(field, value);
    }

    switch (field) {
        case HmiHomeTextField::WaterTemp:
            runtimeIndex = waterTempRuntimeIndex_;
            break;
        case HmiHomeTextField::AirTemp:
            runtimeIndex = airTempRuntimeIndex_;
            break;
        case HmiHomeTextField::Ph:
            runtimeIndex = phRuntimeIndex_;
            break;
        case HmiHomeTextField::Orp:
            runtimeIndex = orpRuntimeIndex_;
            break;
        default:
            return false;
    }

    bool hasValue = false;
    float rawValue = 0.0f;
    if (dsSvc_ && dsSvc_->store && runtimeIndex != kInvalidRuntimeIndex) {
        hasValue = ioEndpointFloat(*dsSvc_->store, runtimeIndex, rawValue);
    }

    switch (field) {
        case HmiHomeTextField::WaterTemp:
            if (hasValue) snprintf(value, sizeof(value), "%.1f\xB0""C", (double)rawValue);
            else snprintf(value, sizeof(value), "--.-\xB0""C");
            break;
        case HmiHomeTextField::AirTemp:
            if (hasValue) snprintf(value, sizeof(value), "%.1f\xB0""C", (double)rawValue);
            else snprintf(value, sizeof(value), "--.-\xB0""C");
            break;
        case HmiHomeTextField::Ph:
            if (hasValue) snprintf(value, sizeof(value), "%.2f", (double)rawValue);
            else snprintf(value, sizeof(value), "--.--");
            break;
        case HmiHomeTextField::Orp:
            if (hasValue) snprintf(value, sizeof(value), "%.0f", (double)rawValue);
            else snprintf(value, sizeof(value), "---");
            break;
        default:
            return false;
    }

    return driver_->publishHomeText(field, value);
}

bool HMIModule::publishHomeGaugePercent_(HmiHomeGaugeField field)
{
    if (!driver_) return false;

    uint8_t runtimeIndex = kInvalidRuntimeIndex;
    float setpoint = 0.0f;
    float phSetpoint = 0.0f;
    float orpSetpoint = 0.0f;
    (void)readPidSetpoints_(phSetpoint, orpSetpoint);

    switch (field) {
        case HmiHomeGaugeField::PhPercent:
            runtimeIndex = phRuntimeIndex_;
            setpoint = phSetpoint;
            break;
        case HmiHomeGaugeField::OrpPercent:
            runtimeIndex = orpRuntimeIndex_;
            setpoint = orpSetpoint;
            break;
        default:
            return false;
    }

    bool hasValue = false;
    float rawValue = 0.0f;
    if (dsSvc_ && dsSvc_->store && runtimeIndex != kInvalidRuntimeIndex) {
        hasValue = ioEndpointFloat(*dsSvc_->store, runtimeIndex, rawValue);
    }

    const uint16_t percent = hasValue ? gaugePercentFromSetpoint_(rawValue, setpoint) : 140U;
    return driver_->publishHomeGaugePercent(field, percent);
}

bool HMIModule::publishHomeStateBits_()
{
    return driver_ && driver_->publishHomeStateBits(buildHomeStateBits_());
}

bool HMIModule::publishHomeAlarmBits_()
{
    return driver_ && driver_->publishHomeAlarmBits(buildHomeAlarmBits_());
}

bool HMIModule::readNextionRtcAndSetTime_()
{
    if (!driver_ || !timeSvc_ || !timeSvc_->setExternalEpoch) return false;

    HmiRtcDateTime rtc{};
    if (!driver_->readRtc(rtc, kNextionRtcReadTimeoutMs)) {
        LOGW("HMI Nextion RTC read failed");
        return false;
    }

    uint64_t epoch = 0ULL;
    if (!rtcDateTimeToEpoch_(rtc, epoch)) {
        LOGW("HMI Nextion RTC invalid %u-%02u-%02u %02u:%02u:%02u",
             (unsigned)rtc.year,
             (unsigned)rtc.month,
             (unsigned)rtc.day,
             (unsigned)rtc.hour,
             (unsigned)rtc.minute,
             (unsigned)rtc.second);
        return false;
    }

    if (!timeSvc_->setExternalEpoch(timeSvc_->ctx, epoch)) {
        LOGW("HMI external RTC import rejected epoch=%llu", (unsigned long long)epoch);
        return false;
    }

    LOGI("HMI imported Nextion RTC %u-%02u-%02u %02u:%02u:%02u",
         (unsigned)rtc.year,
         (unsigned)rtc.month,
         (unsigned)rtc.day,
         (unsigned)rtc.hour,
         (unsigned)rtc.minute,
         (unsigned)rtc.second);
    return true;
}

bool HMIModule::pushEspTimeToNextionRtc_()
{
    if (!driver_ || !timeSvc_ || !timeSvc_->epoch) return false;

    const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
    HmiRtcDateTime rtc{};
    if (!epochToRtcDateTime_(epoch, rtc)) return false;

    if (!driver_->writeRtc(rtc)) {
        LOGW("HMI Nextion RTC write failed");
        return false;
    }

    LOGI("HMI pushed ESP time to Nextion RTC %u-%02u-%02u %02u:%02u:%02u",
         (unsigned)rtc.year,
         (unsigned)rtc.month,
         (unsigned)rtc.day,
         (unsigned)rtc.hour,
         (unsigned)rtc.minute,
         (unsigned)rtc.second);
    return true;
}

void HMIModule::serviceRtcBridge_(uint32_t nowMs)
{
    if (!driver_ || !driverReady_ || !timeSvc_) return;

    const bool timeSynced = timeSvc_->isSynced && timeSvc_->isSynced(timeSvc_->ctx);
    const bool fromExternalRtc = timeSynced &&
                                 timeSvc_->isExternalRtc &&
                                 timeSvc_->isExternalRtc(timeSvc_->ctx);

    if (timeSynced && !fromExternalRtc) {
        rtcFallbackCompleted_ = true;
        if (!timeSvc_->epoch) return;

        const uint64_t epoch = timeSvc_->epoch(timeSvc_->ctx);
        const uint32_t dayStamp = rtcDayStamp_(epoch);
        if (dayStamp == kInvalidClockStamp || dayStamp == lastRtcPushDayStamp_) return;
        if (lastRtcPushAttemptMs_ != 0U &&
            (uint32_t)(nowMs - lastRtcPushAttemptMs_) < kRtcPushRetryMs) {
            return;
        }

        lastRtcPushAttemptMs_ = nowMs;
        if (pushEspTimeToNextionRtc_()) {
            lastRtcPushDayStamp_ = dayStamp;
        }
        return;
    }

    if (rtcFallbackCompleted_) return;

    bool immediateFallback = false;
    if (timeSvc_->state && timeSvc_->state(timeSvc_->ctx) == TimeSyncState::Disabled) {
        immediateFallback = true;
    }
    if (wifiSvc_ && wifiSvc_->state && wifiSvc_->state(wifiSvc_->ctx) == WifiState::Disabled) {
        immediateFallback = true;
    }

    if (!immediateFallback && nowMs < kRtcFallbackDelayMs) return;
    if (lastRtcFallbackAttemptMs_ != 0U &&
        (uint32_t)(nowMs - lastRtcFallbackAttemptMs_) < kRtcFallbackRetryMs) {
        return;
    }

    lastRtcFallbackAttemptMs_ = nowMs;
    if (readNextionRtcAndSetTime_()) {
        rtcFallbackCompleted_ = true;
        queueHomePublish_(kHomePublishTime | kHomePublishDate);
    }
}

void HMIModule::queueHomePublish_(uint32_t mask)
{
    if (mask == 0U) return;
    portENTER_CRITICAL(&homePublishMux_);
    homePublishMask_ |= mask;
    portEXIT_CRITICAL(&homePublishMux_);
}

void HMIModule::queueClockPublishIfDue_(uint32_t nowMs)
{
    if (!driver_ || !driverReady_) return;
    if ((uint32_t)(nowMs - lastClockCheckMs_) < kClockPublishCheckMs) return;
    lastClockCheckMs_ = nowMs;

    const bool synced = timeSvc_ &&
                        timeSvc_->isSynced &&
                        timeSvc_->epoch &&
                        timeSvc_->isSynced(timeSvc_->ctx);
    if (!synced) {
        if (lastClockMinuteStamp_ != kInvalidClockStamp || lastClockDayStamp_ != kInvalidClockStamp) {
            lastClockMinuteStamp_ = kInvalidClockStamp;
            lastClockDayStamp_ = kInvalidClockStamp;
            queueHomePublish_(kHomePublishTime | kHomePublishDate);
        }
        return;
    }

    const time_t epoch = (time_t)timeSvc_->epoch(timeSvc_->ctx);
    struct tm local{};
    if (!localtime_r(&epoch, &local)) return;

    const uint32_t year = (uint32_t)(local.tm_year + 1900);
    const uint32_t minuteStamp = (year * 1000000UL) +
                                 ((uint32_t)local.tm_yday * 1440UL) +
                                 ((uint32_t)local.tm_hour * 60UL) +
                                 (uint32_t)local.tm_min;
    const uint32_t dayStamp = (year * 1000UL) + (uint32_t)local.tm_yday;

    uint32_t mask = 0U;
    if (minuteStamp != lastClockMinuteStamp_) {
        lastClockMinuteStamp_ = minuteStamp;
        mask |= kHomePublishTime;
    }
    if (dayStamp != lastClockDayStamp_) {
        lastClockDayStamp_ = dayStamp;
        mask |= kHomePublishDate;
    }
    queueHomePublish_(mask);
}

void HMIModule::flushHomePublish_()
{
    if (!driver_ || !driverReady_) return;

    uint32_t pending = 0U;
    portENTER_CRITICAL(&homePublishMux_);
    pending = homePublishMask_;
    portEXIT_CRITICAL(&homePublishMux_);
    if (pending == 0U) return;

    uint32_t sent = 0U;
    if ((pending & kHomePublishWaterTemp) != 0U && publishHomeText_(HmiHomeTextField::WaterTemp)) {
        sent |= kHomePublishWaterTemp;
    }
    if ((pending & kHomePublishAirTemp) != 0U && publishHomeText_(HmiHomeTextField::AirTemp)) {
        sent |= kHomePublishAirTemp;
    }
    if ((pending & kHomePublishPh) != 0U && publishHomeText_(HmiHomeTextField::Ph)) {
        sent |= kHomePublishPh;
    }
    if ((pending & kHomePublishOrp) != 0U && publishHomeText_(HmiHomeTextField::Orp)) {
        sent |= kHomePublishOrp;
    }
    if ((pending & kHomePublishPhGauge) != 0U && publishHomeGaugePercent_(HmiHomeGaugeField::PhPercent)) {
        sent |= kHomePublishPhGauge;
    }
    if ((pending & kHomePublishOrpGauge) != 0U && publishHomeGaugePercent_(HmiHomeGaugeField::OrpPercent)) {
        sent |= kHomePublishOrpGauge;
    }
    if ((pending & kHomePublishStateBits) != 0U && publishHomeStateBits_()) {
        sent |= kHomePublishStateBits;
    }
    if ((pending & kHomePublishAlarmBits) != 0U && publishHomeAlarmBits_()) {
        sent |= kHomePublishAlarmBits;
    }
    if ((pending & kHomePublishTime) != 0U && publishHomeText_(HmiHomeTextField::Time)) {
        sent |= kHomePublishTime;
    }
    if ((pending & kHomePublishDate) != 0U && publishHomeText_(HmiHomeTextField::Date)) {
        sent |= kHomePublishDate;
    }
    if ((pending & kHomePublishErrorMessage) != 0U && publishHomeText_(HmiHomeTextField::ErrorMessage)) {
        sent |= kHomePublishErrorMessage;
    }

    if (sent == 0U) return;

    portENTER_CRITICAL(&homePublishMux_);
    homePublishMask_ &= ~sent;
    portEXIT_CRITICAL(&homePublishMux_);
}

void HMIModule::applyLedMask_(bool force)
{
    if (!cfgData_.ledsEnabled) return;
    if (!statusLedsSvc_ || !statusLedsSvc_->setMask) return;

    PoolLogicModeFlags modes{};
    bool wifiConnected = false;
    bool mqttConnected = false;
    if (dsSvc_ && dsSvc_->store) {
        wifiConnected = wifiReady(*dsSvc_->store);
        mqttConnected = mqttReady(*dsSvc_->store);
    }
    (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
    const bool waterLevelLow = isWaterLevelLow_();
    const bool psiAlarm = isAlarmActive_(AlarmId::PoolPsiLow) || isAlarmActive_(AlarmId::PoolPsiHigh);
    const bool phTankLowAlarm = isAlarmActive_(AlarmId::PoolPhTankLow);
    const bool chlorineTankLowAlarm = isAlarmActive_(AlarmId::PoolChlorineTankLow);
    const bool phPumpRuntimeAlarm = isAlarmActive_(AlarmId::PoolPhPumpMaxUptime);
    const bool chlorinePumpRuntimeAlarm = isAlarmActive_(AlarmId::PoolChlorinePumpMaxUptime);

    uint8_t mask = 0U;
    if (wifiConnected && (mqttConnected || wifiBlinkOn_)) {
        mask |= (uint8_t)(1U << kLedBitMqttConnected);
    }

    // Direct mapping: ledPage_=1 drives page 1, ledPage_=2 drives page 2.
    const bool page2 = (ledPage_ == 2U);
    if (!page2) mask |= (uint8_t)(1U << kLedBitPageSelect);

    if (!page2) {
        // Page 1:
        // p2=mode auto, p3=winter, p4/p5 unused, p6=niveau eau bas, p7=PSI error.
        if (modes.autoMode) mask |= (uint8_t)(1U << kLedBitModeA);
        if (modes.winterMode) mask |= (uint8_t)(1U << kLedBitModeB);
        if (waterLevelLow) mask |= (uint8_t)(1U << kLedBitAlarmC);
        if (psiAlarm) mask |= (uint8_t)(1U << kLedBitAlarmD);
    } else {
        // Page 2:
        // p2=mode pH auto, p3=mode ORP auto, p4=bidon pH bas, p5=bidon chlore bas,
        // p6=pompe pH uptime max, p7=pompe chlore uptime max.
        if (modes.phAutoMode) mask |= (uint8_t)(1U << kLedBitModeA);
        if (modes.orpAutoMode) mask |= (uint8_t)(1U << kLedBitModeB);
        if (phTankLowAlarm) mask |= (uint8_t)(1U << kLedBitAlarmA);
        if (chlorineTankLowAlarm) mask |= (uint8_t)(1U << kLedBitAlarmB);
        if (phPumpRuntimeAlarm) mask |= (uint8_t)(1U << kLedBitAlarmC);
        if (chlorinePumpRuntimeAlarm) mask |= (uint8_t)(1U << kLedBitAlarmD);
    }

    if (!force && ledMaskValid_ && ledMaskLast_ == mask) return;
    if (statusLedsSvc_->setMask(statusLedsSvc_->ctx, mask, millis())) {
        ledMaskLast_ = mask;
        ledMaskValid_ = true;
    }
}

bool HMIModule::refreshCurrentModule_()
{
#if !FLOW_HMI_CONFIG_MENU_ENABLED
    return false;
#else
    if (!configMenuReady_) return false;
    const uint8_t prevPage = menu_.pageIndex();
    if (!menu_.refreshCurrent()) return false;
    while (menu_.pageIndex() < prevPage && menu_.nextPage()) {
    }
    return true;
#endif
}

bool HMIModule::ensureConfigMenuReady_()
{
#if FLOW_HMI_CONFIG_MENU_ENABLED
    if (configMenuReady_) return true;
    if (!cfgSvc_) return false;

    const bool okMenu = menu_.begin(cfgSvc_);
    if (!okMenu) {
        LOGE("Config menu init failed free_heap=%u", (unsigned)xPortGetFreeHeapSize());
        return false;
    }

    menu_.setHints(kHints, (uint8_t)(sizeof(kHints) / sizeof(kHints[0])));
    configMenuReady_ = true;
    LOGI("Config menu ready mode=stateless rows_per_page=%u row_cap=%u module_cap=%u free_heap=%u",
         (unsigned)ConfigMenuModel::RowsPerPage,
         (unsigned)ConfigMenuModel::MaxRows,
         (unsigned)ConfigMenuModel::MaxModules,
         (unsigned)xPortGetFreeHeapSize());
    return true;
#else
    return false;
#endif
}

void HMIModule::onEvent_(const Event& e)
{
    bool ledDirty = false;
    uint32_t homePublishMask = 0U;

    if (e.id == EventId::ConfigChanged && e.payload && e.len >= sizeof(ConfigChangedPayload)) {
        const ConfigChangedPayload* p = static_cast<const ConfigChangedPayload*>(e.payload);
#if FLOW_HMI_CONFIG_MENU_ENABLED
        if (configMenuReady_) {
            if (configMenuActive_) {
                viewDirty_ = true;
            }
        }
#endif
        const bool poolLogicChangedByName =
            p->module[0] && strncmp(p->module, "poollogic/", 10) == 0;
        if (p->moduleId == (uint8_t)ConfigModuleId::PoolLogic && !poolLogicChangedByName) {
            LOGW("HMI ignored non-branched PoolLogic config module=%s key=%s",
                 p->module[0] ? p->module : "<empty>",
                 p->nvsKey[0] ? p->nvsKey : "<none>");
        }
        if (poolLogicChangedByName) {
            refreshHomeBindings_();
            ledDirty = true;
            homePublishMask |= kHomePublishAll;
        }
        if (p->module[0] && strncmp(p->module, kHmiModulePrefix, strlen(kHmiModulePrefix)) == 0) {
            applyOutputConfig_();
            ledDirty = cfgData_.ledsEnabled;
            if (cfgData_.nextionEnabled) {
                homePublishMask |= kHomePublishAll;
            }
        }
    } else if (e.id == EventId::DataChanged && e.payload && e.len >= sizeof(DataChangedPayload)) {
        const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
        if (p->id == DATAKEY_WIFI_READY || p->id == DATAKEY_MQTT_READY) {
            ledDirty = true;
            homePublishMask |= kHomePublishStateBits;
        } else if (p->id == DATAKEY_TIME_READY) {
            homePublishMask |= kHomePublishTime | kHomePublishDate;
        } else if (poolLevelRuntimeIndex_ != kInvalidRuntimeIndex &&
                   p->id == (DataKey)(DATAKEY_IO_BASE + poolLevelRuntimeIndex_)) {
            ledDirty = true;
            homePublishMask |= kHomePublishAlarmBits;
        } else if (waterTempRuntimeIndex_ != kInvalidRuntimeIndex &&
                   p->id == (DataKey)(DATAKEY_IO_BASE + waterTempRuntimeIndex_)) {
            homePublishMask |= kHomePublishWaterTemp;
        } else if (airTempRuntimeIndex_ != kInvalidRuntimeIndex &&
                   p->id == (DataKey)(DATAKEY_IO_BASE + airTempRuntimeIndex_)) {
            homePublishMask |= kHomePublishAirTemp;
        } else if (phRuntimeIndex_ != kInvalidRuntimeIndex &&
                   p->id == (DataKey)(DATAKEY_IO_BASE + phRuntimeIndex_)) {
            homePublishMask |= kHomePublishPh | kHomePublishPhGauge;
        } else if (orpRuntimeIndex_ != kInvalidRuntimeIndex &&
                   p->id == (DataKey)(DATAKEY_IO_BASE + orpRuntimeIndex_)) {
            homePublishMask |= kHomePublishOrp | kHomePublishOrpGauge;
        } else if (p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + filtrationDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + phPumpDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + orpPumpDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + robotDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + lightsDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + heaterDeviceSlot_) ||
                   p->id == (DataKey)(DATAKEY_POOL_DEVICE_STATE_BASE + fillingDeviceSlot_)) {
            homePublishMask |= kHomePublishStateBits;
        }
    } else if ((e.id == EventId::AlarmRaised || e.id == EventId::AlarmCleared) &&
               e.payload &&
               e.len >= sizeof(AlarmPayload)) {
        const AlarmPayload* p = static_cast<const AlarmPayload*>(e.payload);
        const AlarmId id = (AlarmId)p->alarmId;
        if (id == AlarmId::PoolPhTankLow ||
            id == AlarmId::PoolChlorineTankLow ||
            id == AlarmId::PoolPhPumpMaxUptime ||
            id == AlarmId::PoolChlorinePumpMaxUptime ||
            id == AlarmId::PoolWaterLevelLow ||
            id == AlarmId::PoolPsiLow ||
            id == AlarmId::PoolPsiHigh) {
            ledDirty = true;
            homePublishMask |= kHomePublishAlarmBits;
        }
    }

    if (ledDirty) {
        applyLedMask_();
    }
    queueHomePublish_(homePublishMask);
}

void HMIModule::handleDriverEvent_(const HmiEvent& e)
{
    if (e.type == HmiEventType::Command) {
        if (executeHmiCommand_(e.command, e.value)) {
            setHomeErrorMessage_("", true);
        }
        return;
    }

    if (e.type == HmiEventType::ConfigEnter) {
        (void)openConfigHome_();
        return;
    }

    if (e.type == HmiEventType::ConfigExit) {
        configMenuActive_ = false;
        viewDirty_ = false;
        return;
    }

#if !FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (e.type == HmiEventType::NextPage) {
            ledPage_ = 2U;
            applyLedMask_(true);
        } else if (e.type == HmiEventType::PrevPage ||
                   e.type == HmiEventType::Home ||
                   e.type == HmiEventType::Back) {
            ledPage_ = 1U;
            applyLedMask_(true);
        }
        return;
    }
#else
    if (!configMenuActive_) {
        if (e.type == HmiEventType::NextPage) {
            ledPage_ = 2U;
            applyLedMask_(true);
        } else if (e.type == HmiEventType::PrevPage ||
                   e.type == HmiEventType::Home ||
                   e.type == HmiEventType::Back) {
            ledPage_ = 1U;
            applyLedMask_(true);
        }
        return;
    }

    bool changed = false;
    switch (e.type) {
        case HmiEventType::Home:
            changed = menu_.home();
            break;
        case HmiEventType::Back:
            if (menu_.isHome()) {
                configMenuActive_ = false;
                viewDirty_ = false;
                return;
            }
            changed = menu_.back();
            break;
        case HmiEventType::Validate: {
            char ack[96]{};
            changed = menu_.validate(ack, sizeof(ack));
            if (!changed) {
                LOGW("Validate failed");
            }
            break;
        }
        case HmiEventType::NextPage:
            changed = menu_.nextPage();
            break;
        case HmiEventType::PrevPage:
            changed = menu_.prevPage();
            break;
        case HmiEventType::RowActivate: {
            changed = menu_.enterRow(e.row);
            if (!changed) {
                ConfigMenuView view{};
                menu_.buildView(view);
                if (e.row < ConfigMenuModel::RowsPerPage && view.rows[e.row].visible) {
                    const ConfigMenuWidget widget = view.rows[e.row].widget;
                    if (widget == ConfigMenuWidget::Switch) changed = menu_.toggleSwitch(e.row);
                    else if (widget == ConfigMenuWidget::Select) changed = menu_.cycleSelect(e.row, 1);
                }
            }
            break;
        }
        case HmiEventType::RowEdit:
            changed = menu_.editRow(e.row);
            break;
        case HmiEventType::RowToggle:
            changed = menu_.toggleSwitch(e.row);
            break;
        case HmiEventType::RowCycle:
            changed = menu_.cycleSelect(e.row, e.direction);
            break;
        case HmiEventType::RowSetText:
            changed = menu_.setText(e.row, e.text);
            break;
        case HmiEventType::RowSetSlider:
            changed = menu_.setSlider(e.row, e.sliderValue);
            break;
        case HmiEventType::ConfigEnter:
        case HmiEventType::ConfigExit:
            break;
        case HmiEventType::None:
        default:
            break;
    }

    if (changed) viewDirty_ = true;
#endif
}

void HMIModule::setHomeErrorMessage_(const char* message, bool forceStateRefresh)
{
    const char* src = (message && message[0] != '\0') ? message : "";
    char next[sizeof(homeErrorMessage_)]{};
    snprintf(next, sizeof(next), "%s", src);

    if (strncmp(homeErrorMessage_, next, sizeof(homeErrorMessage_)) != 0) {
        snprintf(homeErrorMessage_, sizeof(homeErrorMessage_), "%s", next);
    }

    uint32_t mask = kHomePublishErrorMessage;
    if (forceStateRefresh) mask |= kHomePublishStateBits;
    queueHomePublish_(mask);
    if (driverReady_) {
        flushHomePublish_();
    }
}

void HMIModule::reportCommandError_(const char* operation, const char* reply)
{
    char code[40]{};
    char where[48]{};
    const bool haveCode = extractJsonStringField_(reply, "code", code, sizeof(code));
    const bool haveWhere = extractJsonStringField_(reply, "where", where, sizeof(where));

    char message[sizeof(homeErrorMessage_)]{};
    if (haveCode && haveWhere) {
        snprintf(message, sizeof(message), "%s (%s)", code, where);
    } else if (haveCode) {
        snprintf(message, sizeof(message), "%s", code);
    } else if (operation && operation[0] != '\0') {
        snprintf(message, sizeof(message), "%s failed", operation);
    } else {
        snprintf(message, sizeof(message), "Command failed");
    }

    setHomeErrorMessage_(message, true);
}

bool HMIModule::executeHmiCommand_(HmiCommandId command, uint8_t value)
{
    bool current = false;
    PoolLogicModeFlags modes{};

    switch (command) {
        case HmiCommandId::HomeSyncRequest:
            queueHomePublish_(kHomePublishAll);
            return true;

        case HmiCommandId::HomeConfigOpen:
            // Nextion owns visual navigation. The config menu is opened only
            // when pageCfgMenu announces itself with the PAGE frame.
            return true;

        case HmiCommandId::HomeFiltrationSet:
            return executeCommandBool_("poollogic.filtration.write", value != 0U);

        case HmiCommandId::HomeAutoModeSet:
            return executePoolLogicModePatch_("auto_mode", value != 0U);

        case HmiCommandId::HomePhPumpSet:
            return executePoolDeviceWrite_(phPumpDeviceSlot_, value != 0U);

        case HmiCommandId::HomeOrpPumpSet:
            return executePoolDeviceWrite_(orpPumpDeviceSlot_, value != 0U);

        case HmiCommandId::HomePhPumpToggle:
            if (!readPoolDeviceActualOn_(phPumpDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executePoolDeviceWrite_(phPumpDeviceSlot_, !current);

        case HmiCommandId::HomeOrpPumpToggle:
            if (!readPoolDeviceActualOn_(orpPumpDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executePoolDeviceWrite_(orpPumpDeviceSlot_, !current);

        case HmiCommandId::HomeFiltrationToggle:
            if (!readPoolDeviceActualOn_(filtrationDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executeCommandBool_("poollogic.filtration.write", !current);

        case HmiCommandId::HomeAutoModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("auto_mode", !modes.autoMode);

        case HmiCommandId::HomeOrpAutoModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("orp_auto_mode", !modes.orpAutoMode);

        case HmiCommandId::HomePhAutoModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("ph_auto_mode", !modes.phAutoMode);

        case HmiCommandId::HomeWinterModeToggle:
            (void)readPoolLogicModeFlags_(modes.autoMode, modes.winterMode, modes.phAutoMode, modes.orpAutoMode);
            return executePoolLogicModePatch_("winter_mode", !modes.winterMode);

        case HmiCommandId::HomeLightsToggle:
            if (!readPoolDeviceActualOn_(lightsDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executePoolDeviceWrite_(lightsDeviceSlot_, !current);

        case HmiCommandId::HomeRobotToggle:
            if (!readPoolDeviceActualOn_(robotDeviceSlot_, current)) {
                setHomeErrorMessage_("ReadStateFailed", true);
                return false;
            }
            return executePoolDeviceWrite_(robotDeviceSlot_, !current);

        case HmiCommandId::None:
        default:
            setHomeErrorMessage_("UnknownCmd", true);
            return false;
    }
}

bool HMIModule::executeCommandBool_(const char* cmdName, bool value)
{
    if (!cmdName || cmdName[0] == '\0') return false;
    if (!cmdSvc_ || !cmdSvc_->execute) {
        LOGW("HMI command service unavailable cmd=%s", cmdName);
        setHomeErrorMessage_("CmdServiceUnavailable", true);
        return false;
    }

    char args[32]{};
    char reply[192]{};
    snprintf(args, sizeof(args), "{\"value\":%s}", value ? "true" : "false");

    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, cmdName, args, nullptr, reply, sizeof(reply));
    if (!ok) {
        LOGW("HMI command failed cmd=%s args=%s reply=%s",
             cmdName,
             args,
             reply[0] ? reply : "{}");
        reportCommandError_(cmdName, reply[0] ? reply : nullptr);
        return false;
    }

    LOGI("HMI command ok cmd=%s args=%s", cmdName, args);
    queueHomePublish_(kHomePublishAll);
    return true;
}

bool HMIModule::executePoolDeviceWrite_(uint8_t slot, bool value)
{
    if (!cmdSvc_ || !cmdSvc_->execute) {
        LOGW("HMI command service unavailable cmd=pooldevice.write");
        setHomeErrorMessage_("CmdServiceUnavailable", true);
        return false;
    }

    char args[40]{};
    char reply[192]{};
    snprintf(args, sizeof(args), "{\"slot\":%u,\"value\":%s}", (unsigned)slot, value ? "true" : "false");

    const bool ok = cmdSvc_->execute(cmdSvc_->ctx, "pooldevice.write", args, nullptr, reply, sizeof(reply));
    if (!ok) {
        LOGW("HMI command failed cmd=pooldevice.write args=%s reply=%s",
             args,
             reply[0] ? reply : "{}");
        reportCommandError_("pooldevice.write", reply[0] ? reply : nullptr);
        return false;
    }

    LOGI("HMI command ok cmd=pooldevice.write args=%s", args);
    queueHomePublish_(kHomePublishAll);
    return true;
}

bool HMIModule::executePoolLogicModePatch_(const char* key, bool value)
{
    if (!key || key[0] == '\0') return false;
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        LOGW("HMI config service unavailable key=%s", key);
        setHomeErrorMessage_("CfgServiceUnavailable", true);
        return false;
    }

    char json[96]{};
    snprintf(json, sizeof(json), "{\"poollogic/mode\":{\"%s\":%s}}", key, value ? "true" : "false");
    const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, json);
    if (!ok) {
        LOGW("HMI config patch failed json=%s", json);
        reportCommandError_("cfg.applyJson", "{\"ok\":false,\"err\":{\"code\":\"CfgApplyFailed\",\"where\":\"cfg.applyJson\"}}");
        return false;
    }

    LOGI("HMI config patch ok json=%s", json);
    queueHomePublish_(kHomePublishAll);
    return true;
}

bool HMIModule::render_()
{
    if (!driver_) return false;
    ConfigMenuView view{};
#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (!ensureConfigMenuReady_()) return false;
        menu_.buildView(view);
    }
#else
    {
        snprintf(view.breadcrumb, sizeof(view.breadcrumb), "Configuration indisponible");
        view.pageIndex = 0;
        view.pageCount = 1;
        view.canHome = false;
        view.canBack = false;
        view.canValidate = false;
        view.isHome = true;
        view.rowCountOnPage = 1;
        view.rows[0].visible = true;
        view.rows[0].editable = false;
        snprintf(view.rows[0].key, sizeof(view.rows[0].key), "menu");
        snprintf(view.rows[0].label, sizeof(view.rows[0].label), "Config");
        snprintf(view.rows[0].value, sizeof(view.rows[0].value), "disabled");
    }
#endif
    const bool ok = driver_->renderConfigMenu(view);
    if (ok) {
        lastRenderMs_ = millis();
        lastConfigValueRefreshMs_ = lastRenderMs_;
    }
    return ok;
}

bool HMIModule::refreshConfigMenuValues_()
{
    if (!driver_) return false;

#if FLOW_HMI_CONFIG_MENU_ENABLED
    {
        if (!ensureConfigMenuReady_()) return false;
        ConfigMenuView view{};
        menu_.buildView(view);
        const bool ok = driver_->refreshConfigMenuValues(view);
        if (ok) lastConfigValueRefreshMs_ = millis();
        return ok;
    }
#else
    return false;
#endif
}

bool HMIModule::buildMenuJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;

#if !FLOW_HMI_CONFIG_MENU_ENABLED
    {
        DynamicJsonDocument doc(256);
        JsonObject root = doc.to<JsonObject>();
        root["ok"] = true;
        root["disabled"] = true;
        root["driver"] = driver_ ? driver_->driverId() : "";
        root["path"] = "Configuration indisponible";
        root["page"] = 1U;
        root["pages"] = 1U;
        root["rows"] = 0U;
        root["can_home"] = false;
        root["can_back"] = false;
        root["can_validate"] = false;
        const size_t written = serializeJson(root, out, outLen);
        return written > 0 && written < outLen;
    }
#else
    if (!ensureConfigMenuReady_()) return false;
    ConfigMenuView view{};
    menu_.buildView(view);

    DynamicJsonDocument doc(2048);
    JsonObject root = doc.to<JsonObject>();
    root["ok"] = true;
    root["driver"] = driver_ ? driver_->driverId() : "";
    root["path"] = view.breadcrumb;
    root["page"] = (uint32_t)view.pageIndex + 1U;
    root["pages"] = (uint32_t)view.pageCount;
    root["rows"] = (uint32_t)view.rowCountOnPage;
    root["mode"] = (view.mode == ConfigMenuMode::Edit) ? "edit" : "browse";
    root["can_home"] = view.canHome;
    root["can_back"] = view.canBack;
    root["can_validate"] = view.canValidate;

    JsonArray arr = root.createNestedArray("items");
    for (uint8_t i = 0; i < ConfigMenuModel::RowsPerPage; ++i) {
        const ConfigMenuRowView& row = view.rows[i];
        if (!row.visible) continue;
        JsonObject it = arr.createNestedObject();
        it["i"] = i;
        it["key"] = row.key;
        it["label"] = row.label;
        it["value"] = row.value;
        it["value_visible"] = row.valueVisible;
        it["can_enter"] = row.canEnter;
        it["can_edit"] = row.canEdit;
        it["editable"] = row.editable;
        it["dirty"] = row.dirty;
        it["widget"] = (uint8_t)row.widget;
    }

    const size_t written = serializeJson(root, out, outLen);
    return written > 0 && written < outLen;
#endif
}

void HMIModule::loop()
{
    if (driver_) {
        if (!driverReady_) {
            driverReady_ = driver_->begin();
            if (!driverReady_) {
                venice_.tick(millis(), ioSvc_, waterTempIoId_);
                vTaskDelay(pdMS_TO_TICKS(500));
                return;
            }
            if (driver_ == static_cast<IHmiDriver*>(&nextion_)) {
                nextionVersionDetected_ = nextion_.hasDisplayVersion();
                nextionVersion_ = nextion_.displayVersion();
                if (nextionVersionDetected_) {
                    LOGI("Ecran Nextion version %04lu detecte.", (unsigned long)nextionVersion_);
                    if (nextionVersion_ != kNextionDisplayVersionCurrent) {
                        LOGW("Ecran Nextion version %04lu non supportee (attendu %04lu). Affichage Nextion desactive.",
                             (unsigned long)nextionVersion_,
                             (unsigned long)kNextionDisplayVersionCurrent);
                        nextionDisabledByVersion_ = true;
                        applyOutputConfig_();
                        vTaskDelay(pdMS_TO_TICKS(25));
                        return;
                    }
                } else {
                    LOGW("Ecran Nextion version non detectee. Affichage Nextion desactive.");
                    nextionDisabledByVersion_ = true;
                    applyOutputConfig_();
                    vTaskDelay(pdMS_TO_TICKS(25));
                    return;
                }
            }
            viewDirty_ = true;
            queueHomePublish_(kHomePublishAll);
        }

        HmiEvent ev{};
        while (driver_->pollEvent(ev)) {
            handleDriverEvent_(ev);
        }
    } else {
        driverReady_ = false;
    }

    const uint32_t now = millis();
    if (cfgData_.ledsEnabled) {
        bool wifiConnected = false;
        bool mqttConnected = false;
        if (dsSvc_ && dsSvc_->store) {
            wifiConnected = wifiReady(*dsSvc_->store);
            mqttConnected = mqttReady(*dsSvc_->store);
        }

        if (wifiConnected && !mqttConnected) {
            if ((uint32_t)(now - lastWifiBlinkToggleMs_) >= kWifiBlinkPeriodMs) {
                lastWifiBlinkToggleMs_ = now;
                wifiBlinkOn_ = !wifiBlinkOn_;
                applyLedMask_(true);
            }
        } else if (wifiBlinkOn_) {
            wifiBlinkOn_ = false;
            lastWifiBlinkToggleMs_ = now;
            applyLedMask_(true);
        }

        if (statusLedsSvc_ && (uint32_t)(now - lastLedPageToggleMs_) >= kLedPageTogglePeriodMs) {
            lastLedPageToggleMs_ = now;
            const uint8_t nextPage = (ledPage_ == 1U) ? 2U : 1U;
            ledPage_ = nextPage;
            applyLedMask_(true);
        }
        if (!ledMaskValid_ || (uint32_t)(now - lastLedApplyTryMs_) >= 1000U) {
            applyLedMask_();
            lastLedApplyTryMs_ = now;
        }
    }
    serviceRtcBridge_(now);
    queueClockPublishIfDue_(now);
    flushHomePublish_();
    if (kConfigMenuEnabled && configMenuActive_ && driver_ && viewDirty_) {
        if (render_()) {
            viewDirty_ = false;
        }
    } else if (kConfigMenuEnabled &&
               configMenuActive_ &&
               driver_ &&
               (uint32_t)(now - lastConfigValueRefreshMs_) >= 5000U) {
        (void)refreshConfigMenuValues_();
    }

    if (driver_) driver_->tick(now);
    venice_.tick(now, ioSvc_, waterTempIoId_);
    vTaskDelay(pdMS_TO_TICKS(25));
}
