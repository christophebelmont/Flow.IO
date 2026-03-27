/**
 * @file SupervisorHMIModule.cpp
 * @brief Implementation file.
 */

#include "Modules/SupervisorHMIModule/SupervisorHMIModule.h"

#include "Board/BoardSpec.h"
#include "Core/DataKeys.h"
#include "Core/EventBus/EventPayloads.h"
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientRuntime.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HMIModule)
#include "Core/ModuleLog.h"

#include "App/FirmwareProfile.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_system.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

namespace {
static constexpr uint32_t kFwPollMs = 500U;
static constexpr uint32_t kStartupSplashHoldMs = 5000U;
static constexpr uint32_t kStartupBacklightForceOnMs = 3600000U;
static constexpr uint32_t kButtonBootGuardMs = 8000U;
static constexpr uint32_t kButtonArmHighStableMs = 500U;
static constexpr uint32_t kPageRotateMs = 10000U;

const SupervisorBoardSpec& supervisorBoardSpec_(const BoardSpec& board)
{
    static constexpr SupervisorBoardSpec kFallback{
        {
            240,
            320,
            1,
            0,
            33,
            14,
            15,
            4,
            5,
            19,
            18,
            true,
            false,
            40000000U,
            80
        },
        {
            36,
            120,
            true,
            23,
            40
        },
        {
            25,
            26,
            13,
            115200U
        }
    };
    const SupervisorBoardSpec* cfg = boardSupervisorConfig(board);
    return cfg ? *cfg : kFallback;
}

} // namespace

SupervisorHMIModule::SupervisorHMIModule(const BoardSpec& board, const SupervisorRuntimeOptions& runtime)
    : driverCfg_(makeDriverConfig_(board)),
      driver_(driverCfg_)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    pirPin_ = boardCfg.inputs.pirPin;
    wifiResetPin_ = boardCfg.inputs.wifiResetPin;
    pirTimeoutMs_ = runtime.pirTimeoutMs;
    pirDebounceMs_ = boardCfg.inputs.pirDebounceMs;
    pirActiveHigh_ = boardCfg.inputs.pirActiveHigh;
    wifiResetHoldMs_ = runtime.wifiResetHoldMs;
    wifiResetDebounceMs_ = boardCfg.inputs.wifiResetDebounceMs;
}

St7789SupervisorDriverConfig SupervisorHMIModule::makeDriverConfig_(const BoardSpec& board)
{
    const SupervisorBoardSpec& boardCfg = supervisorBoardSpec_(board);
    St7789SupervisorDriverConfig cfg{};
    cfg.resX = boardCfg.display.resX;
    cfg.resY = boardCfg.display.resY;
    cfg.rotation = boardCfg.display.rotation;
    cfg.colStart = boardCfg.display.colStart;
    cfg.rowStart = boardCfg.display.rowStart;
    cfg.backlightPin = boardCfg.display.backlightPin;
    cfg.csPin = boardCfg.display.csPin;
    cfg.dcPin = boardCfg.display.dcPin;
    cfg.rstPin = boardCfg.display.rstPin;
    cfg.mosiPin = boardCfg.display.mosiPin;
    cfg.sclkPin = boardCfg.display.sclkPin;
    cfg.swapColorBytes = boardCfg.display.swapColorBytes;
    cfg.invertColors = boardCfg.display.invertColors;
    cfg.spiHz = boardCfg.display.spiHz;
    cfg.minRenderGapMs = boardCfg.display.minRenderGapMs;
    return cfg;
}

void SupervisorHMIModule::copyText_(char* out, size_t outLen, const char* in)
{
    if (!out || outLen == 0) return;
    if (!in) in = "";
    snprintf(out, outLen, "%s", in);
}

uint32_t SupervisorHMIModule::buildRenderKey_() const
{
    uint32_t h = 2166136261U;
    auto mix = [&h](const void* ptr, size_t len) {
        if (!ptr || len == 0) return;
        const uint8_t* b = reinterpret_cast<const uint8_t*>(ptr);
        for (size_t i = 0; i < len; i++) {
            h ^= (uint32_t)b[i];
            h *= 16777619U;
        }
    };

    mix(&view_.wifiConnected, sizeof(view_.wifiConnected));
    mix(&view_.wifiState, sizeof(view_.wifiState));
    mix(view_.ip, strnlen(view_.ip, sizeof(view_.ip)) + 1U);
    mix(&view_.flowLinkOk, sizeof(view_.flowLinkOk));
    mix(&view_.flowMqttReady, sizeof(view_.flowMqttReady));
    mix(&view_.flowHasPoolModes, sizeof(view_.flowHasPoolModes));
    mix(&view_.flowFiltrationAuto, sizeof(view_.flowFiltrationAuto));
    mix(&view_.flowWinterMode, sizeof(view_.flowWinterMode));
    mix(&view_.flowPhAutoMode, sizeof(view_.flowPhAutoMode));
    mix(&view_.flowOrpAutoMode, sizeof(view_.flowOrpAutoMode));
    mix(&view_.flowFiltrationOn, sizeof(view_.flowFiltrationOn));
    mix(&view_.flowPhPumpOn, sizeof(view_.flowPhPumpOn));
    mix(&view_.flowChlorinePumpOn, sizeof(view_.flowChlorinePumpOn));
    mix(&view_.flowHasPh, sizeof(view_.flowHasPh));
    mix(&view_.flowPhValue, sizeof(view_.flowPhValue));
    mix(&view_.flowHasOrp, sizeof(view_.flowHasOrp));
    mix(&view_.flowOrpValue, sizeof(view_.flowOrpValue));
    mix(&view_.flowHasWaterTemp, sizeof(view_.flowHasWaterTemp));
    mix(&view_.flowWaterTemp, sizeof(view_.flowWaterTemp));
    mix(&view_.flowHasAirTemp, sizeof(view_.flowHasAirTemp));
    mix(&view_.flowAirTemp, sizeof(view_.flowAirTemp));
    mix(&view_.flowAlarmActiveCount, sizeof(view_.flowAlarmActiveCount));
    mix(&view_.flowAlarmCodeCount, sizeof(view_.flowAlarmCodeCount));
    for (size_t i = 0; i < (sizeof(view_.flowAlarmCodes) / sizeof(view_.flowAlarmCodes[0])); i++) {
        mix(view_.flowAlarmCodes[i], strnlen(view_.flowAlarmCodes[i], sizeof(view_.flowAlarmCodes[i])) + 1U);
    }
    mix(&view_.flowAlarmActCount, sizeof(view_.flowAlarmActCount));
    mix(&view_.flowAlarmAckCount, sizeof(view_.flowAlarmAckCount));
    mix(&view_.flowAlarmClrCount, sizeof(view_.flowAlarmClrCount));
    mix(view_.flowAlarmStates, sizeof(view_.flowAlarmStates));
    mix(&view_.wifiResetPending, sizeof(view_.wifiResetPending));
    mix(view_.banner, strnlen(view_.banner, sizeof(view_.banner)) + 1U);

    return h;
}

uint32_t SupervisorHMIModule::currentClockMinute_() const
{
    const time_t now = time(nullptr);
    if (now <= 1600000000) return 0U;
    return (uint32_t)(now / 60);
}

uint32_t SupervisorHMIModule::currentPageCycle_() const
{
    return (uint32_t)(millis() / kPageRotateMs);
}

void SupervisorHMIModule::onEventStatic_(const Event& e, void* user)
{
    SupervisorHMIModule* self = static_cast<SupervisorHMIModule*>(user);
    if (self) self->onEvent_(e);
}

void SupervisorHMIModule::init(ConfigStore&, ServiceRegistry& services)
{
    logHub_ = services.get<LogHubService>(ServiceId::LogHub);
    cfgSvc_ = services.get<ConfigStoreService>(ServiceId::ConfigStore);
    eventBusSvc_ = services.get<EventBusService>(ServiceId::EventBus);
    dsSvc_ = services.get<DataStoreService>(ServiceId::DataStore);
    wifiSvc_ = services.get<WifiService>(ServiceId::Wifi);
    netAccessSvc_ = services.get<NetworkAccessService>(ServiceId::NetworkAccess);
    fwUpdateSvc_ = services.get<FirmwareUpdateService>(ServiceId::FirmwareUpdate);
    eventBus_ = eventBusSvc_ ? eventBusSvc_->bus : nullptr;
    (void)logHub_;

    if (eventBus_) {
        eventBus_->subscribe(EventId::DataChanged, &SupervisorHMIModule::onEventStatic_, this);
    }

    if (pirPin_ >= 0) {
        int pirMode = INPUT;
#if defined(ESP32)
        // ESP32 GPIO 34..39 are input-only and don't support internal pull resistors.
        if (pirPin_ <= 33) {
            pirMode = pirActiveHigh_ ? INPUT_PULLDOWN : INPUT_PULLUP;
        }
#endif
        pinMode(pirPin_, pirMode);
        const bool pirLevelHigh = (digitalRead(pirPin_) == HIGH);
        const bool rawPir = pirActiveHigh_ ? pirLevelHigh : !pirLevelHigh;
        pirRawState_ = rawPir;
        pirStableState_ = rawPir;
        pirDebounceChangedAtMs_ = millis();
    }
    if (wifiResetPin_ >= 0) {
        pinMode(wifiResetPin_, INPUT_PULLUP);
        const bool rawPressed = (digitalRead(wifiResetPin_) == LOW);
        buttonRawPressed_ = rawPressed;
        buttonStablePressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = millis();
    }

    lastMotionMs_ = millis();
    copyText_(view_.fwState, sizeof(view_.fwState), "idle");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), "none");
    setDefaultBanner_();
    refreshFlowStatusFromDataStore_();

    LOGI("Supervisor HMI initialized tft_cs=%d tft_dc=%d tft_rst=%d tft_bl=%d tft_mosi=%d tft_sclk=%d rot=%d colstart=%d rowstart=%d pir=%d pir_active_high=%d wifi_reset=%d",
         (int)driverCfg_.csPin,
         (int)driverCfg_.dcPin,
         (int)driverCfg_.rstPin,
         (int)driverCfg_.backlightPin,
         (int)driverCfg_.mosiPin,
         (int)driverCfg_.sclkPin,
         (int)driverCfg_.rotation,
         (int)driverCfg_.colStart,
         (int)driverCfg_.rowStart,
         (int)pirPin_,
         (int)(pirActiveHigh_ ? 1 : 0),
         (int)wifiResetPin_);
}

void SupervisorHMIModule::onEvent_(const Event& e)
{
    if (e.id != EventId::DataChanged) return;
    if (!e.payload || e.len < sizeof(DataChangedPayload)) return;
    const DataChangedPayload* p = static_cast<const DataChangedPayload*>(e.payload);
    if (p->id < DataKeys::FlowRemoteBase || p->id >= DataKeys::FlowRemoteEndExclusive) return;
    flowRuntimeDirty_ = true;
}

void SupervisorHMIModule::pollWifiAndNetwork_()
{
    view_.wifiConnected = false;
    view_.wifiState = WifiState::Idle;
    view_.accessMode = NetworkAccessMode::None;
    view_.netReachable = false;
    view_.hasRssi = false;
    view_.rssiDbm = -127;
    view_.ip[0] = '\0';

    if (wifiSvc_) {
        if (wifiSvc_->state) view_.wifiState = wifiSvc_->state(wifiSvc_->ctx);
        if (wifiSvc_->isConnected) view_.wifiConnected = wifiSvc_->isConnected(wifiSvc_->ctx);
        if (wifiSvc_->getIP) (void)wifiSvc_->getIP(wifiSvc_->ctx, view_.ip, sizeof(view_.ip));
    }

    if (netAccessSvc_) {
        if (netAccessSvc_->mode) view_.accessMode = netAccessSvc_->mode(netAccessSvc_->ctx);
        if (netAccessSvc_->isWebReachable) view_.netReachable = netAccessSvc_->isWebReachable(netAccessSvc_->ctx);
        if (view_.ip[0] == '\0' && netAccessSvc_->getIP) {
            (void)netAccessSvc_->getIP(netAccessSvc_->ctx, view_.ip, sizeof(view_.ip));
        }
    }

    if (view_.wifiConnected && WiFi.status() == WL_CONNECTED) {
        view_.rssiDbm = (int32_t)WiFi.RSSI();
        view_.hasRssi = true;
    }
}

void SupervisorHMIModule::pollFirmwareStatus_()
{
    if (!fwUpdateSvc_ || !fwUpdateSvc_->statusJson) return;

    char json[320] = {0};
    if (!fwUpdateSvc_->statusJson(fwUpdateSvc_->ctx, json, sizeof(json))) return;

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) return;

    JsonObjectConst root = doc.as<JsonObjectConst>();
    copyText_(view_.fwState, sizeof(view_.fwState), root["state"] | "n/a");
    copyText_(view_.fwTarget, sizeof(view_.fwTarget), root["target"] | "n/a");
    view_.fwProgress = (uint8_t)(root["progress"] | 0U);
    copyText_(view_.fwMsg, sizeof(view_.fwMsg), root["msg"] | "");
    view_.fwBusy = root["busy"] | false;
    view_.fwPending = root["pending"] | false;
    fwBusyOrPending_ = view_.fwBusy || view_.fwPending;
}

void SupervisorHMIModule::refreshFlowStatusFromDataStore_()
{
    if (!dsSvc_ || !dsSvc_->store) return;
    const FlowRemoteRuntimeData& flow = flowRemoteRuntime(*dsSvc_->store);
    view_.flowCfgReady = flow.ready;
    view_.flowLinkOk = flow.linkOk;
    copyText_(view_.flowFirmware, sizeof(view_.flowFirmware), flow.firmware);
    view_.flowHasRssi = flow.hasRssi;
    view_.flowRssiDbm = flow.rssiDbm;
    view_.flowHasHeapFrag = flow.hasHeapFrag;
    view_.flowHeapFragPct = flow.heapFragPct;
    view_.flowMqttReady = flow.mqttReady;
    view_.flowMqttRxDrop = flow.mqttRxDrop;
    view_.flowMqttParseFail = flow.mqttParseFail;
    view_.flowI2cReqCount = flow.i2cReqCount;
    view_.flowI2cBadReqCount = flow.i2cBadReqCount;
    view_.flowI2cLastReqAgoMs = flow.i2cLastReqAgoMs;
    view_.flowHasPoolModes = flow.hasPoolModes;
    view_.flowFiltrationAuto = flow.filtrationAuto;
    view_.flowWinterMode = flow.winterMode;
    view_.flowPhAutoMode = flow.phAutoMode;
    view_.flowOrpAutoMode = flow.orpAutoMode;
    view_.flowFiltrationOn = flow.filtrationOn;
    view_.flowPhPumpOn = flow.phPumpOn;
    view_.flowChlorinePumpOn = flow.chlorinePumpOn;
    view_.flowHasPh = flow.hasPh;
    view_.flowPhValue = flow.phValue;
    view_.flowHasOrp = flow.hasOrp;
    view_.flowOrpValue = flow.orpValue;
    view_.flowHasWaterTemp = flow.hasWaterTemp;
    view_.flowWaterTemp = flow.waterTemp;
    view_.flowHasAirTemp = flow.hasAirTemp;
    view_.flowAirTemp = flow.airTemp;
}

void SupervisorHMIModule::triggerWifiReset_()
{
    if (wifiResetPending_) return;
    if (!cfgSvc_ || !cfgSvc_->applyJson) {
        copyText_(view_.banner, sizeof(view_.banner), "WiFi reset failed: config unavailable");
        return;
    }

    static constexpr const char* kWifiResetPatch = "{\"wifi\":{\"enabled\":true,\"ssid\":\"\",\"pass\":\"\"}}";
    const bool ok = cfgSvc_->applyJson(cfgSvc_->ctx, kWifiResetPatch);
    if (!ok) {
        copyText_(view_.banner, sizeof(view_.banner), "WiFi reset failed: applyJson");
        return;
    }

    if (netAccessSvc_ && netAccessSvc_->notifyWifiConfigChanged) {
        (void)netAccessSvc_->notifyWifiConfigChanged(netAccessSvc_->ctx);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }

    wifiResetPending_ = true;
    restartAtMs_ = millis() + 1200U;
    copyText_(view_.banner, sizeof(view_.banner), "WiFi credentials reset: rebooting...");
    LOGW("WiFi credentials reset requested from local button");
}

void SupervisorHMIModule::updateWifiResetButton_()
{
    if (wifiResetPin_ < 0) return;

    const uint32_t now = millis();
    const bool rawPressed = (digitalRead(wifiResetPin_) == LOW);

    if (rawPressed != buttonRawPressed_) {
        buttonRawPressed_ = rawPressed;
        buttonDebounceChangedAtMs_ = now;
    }
    if ((uint32_t)(now - buttonDebounceChangedAtMs_) >= wifiResetDebounceMs_) {
        buttonStablePressed_ = buttonRawPressed_;
    }

    const bool isPressed = buttonStablePressed_;

    // Safety: do not accept press events until we've seen a stable released
    // level after boot. This avoids false long-press triggers on floating lines.
    if (!buttonArmed_) {
        if (isPressed) {
            buttonHighSinceMs_ = 0;
            buttonPressed_ = false;
            buttonTriggered_ = false;
            return;
        }

        if (buttonHighSinceMs_ == 0U) {
            buttonHighSinceMs_ = now;
            return;
        }

        if ((uint32_t)(now - buttonHighSinceMs_) < kButtonArmHighStableMs) {
            return;
        }
        if (now < kButtonBootGuardMs) {
            return;
        }

        buttonArmed_ = true;
        LOGI("WiFi reset button armed");
        return;
    }

    if (!isPressed) {
        buttonPressed_ = false;
        buttonTriggered_ = false;
        return;
    }

    if (!buttonPressed_) {
        buttonPressed_ = true;
        buttonPressedAtMs_ = now;
        return;
    }

    if (buttonTriggered_) return;
    if ((uint32_t)(now - buttonPressedAtMs_) < wifiResetHoldMs_) return;

    buttonTriggered_ = true;
    triggerWifiReset_();
}

void SupervisorHMIModule::updateBacklight_()
{
    const uint32_t now = millis();
    const bool forceBacklightOn = (int32_t)(now - backlightForceOnUntilMs_) < 0;
    bool motion = false;
    if (pirPin_ >= 0) {
        const bool pirLevelHigh = (digitalRead(pirPin_) == HIGH);
        const bool rawMotion = pirActiveHigh_ ? pirLevelHigh : !pirLevelHigh;
        if (rawMotion != pirRawState_) {
            pirRawState_ = rawMotion;
            pirDebounceChangedAtMs_ = now;
        }
        if ((uint32_t)(now - pirDebounceChangedAtMs_) >= pirDebounceMs_) {
            pirStableState_ = pirRawState_;
        }
        motion = pirStableState_;
    }
    if (motion || fwBusyOrPending_) {
        lastMotionMs_ = now;
    }

    if (pirPin_ < 0) {
        driver_.setBacklight(true);
        return;
    }

    const bool keepOn = forceBacklightOn || fwBusyOrPending_ || ((uint32_t)(now - lastMotionMs_) <= pirTimeoutMs_);
    driver_.setBacklight(keepOn);
}

void SupervisorHMIModule::rebuildBanner_()
{
    if (wifiResetPending_) return;
    if (!driver_.isBacklightOn()) {
        copyText_(view_.banner, sizeof(view_.banner), "PIR idle: backlight off");
        return;
    }
    setDefaultBanner_();
}

void SupervisorHMIModule::setDefaultBanner_()
{
    if (wifiResetPin_ < 0) {
        copyText_(view_.banner, sizeof(view_.banner), "Local display ready");
        return;
    }
    const uint32_t holdSeconds = (wifiResetHoldMs_ + 999U) / 1000U;
    snprintf(view_.banner, sizeof(view_.banner), "Hold WiFi button %lus to reset credentials", (unsigned long)holdSeconds);
}

void SupervisorHMIModule::loop()
{
    if (!driverReady_) {
        driverReady_ = driver_.begin();
        if (!driverReady_) {
            vTaskDelay(pdMS_TO_TICKS(500));
            return;
        }
        lastRenderMs_ = 0;
        splashHoldUntilMs_ = millis() + kStartupSplashHoldMs;
        backlightForceOnUntilMs_ = millis() + kStartupBacklightForceOnMs;
        hasLastRenderKey_ = false;
        lastRenderedMinute_ = 0;
        lastRenderedPageCycle_ = 0;
        lastBacklightOn_ = driver_.isBacklightOn();
    }

    pollWifiAndNetwork_();

    const uint32_t now = millis();
    if ((uint32_t)(now - lastFwPollMs_) >= kFwPollMs) {
        lastFwPollMs_ = now;
        pollFirmwareStatus_();
    }

    if (flowRuntimeDirty_) {
        flowRuntimeDirty_ = false;
        refreshFlowStatusFromDataStore_();
    }

    updateWifiResetButton_();
    updateBacklight_();
    rebuildBanner_();

    view_.wifiResetPending = wifiResetPending_;

    if ((int32_t)(millis() - splashHoldUntilMs_) < 0) {
        vTaskDelay(pdMS_TO_TICKS(25));
        return;
    }

    const bool backlightOn = driver_.isBacklightOn();
    const uint32_t renderKey = buildRenderKey_();
    const uint32_t minuteKey = currentClockMinute_();
    const uint32_t pageCycleKey = currentPageCycle_();
    const bool changed = (!hasLastRenderKey_) ||
                         (renderKey != lastRenderKey_) ||
                         (minuteKey != lastRenderedMinute_) ||
                         (pageCycleKey != lastRenderedPageCycle_) ||
                         (backlightOn != lastBacklightOn_);
    if (changed && backlightOn) {
        (void)driver_.render(view_, !hasLastRenderKey_);
        lastRenderMs_ = now;
        lastRenderKey_ = renderKey;
        lastRenderedMinute_ = minuteKey;
        lastRenderedPageCycle_ = pageCycleKey;
        hasLastRenderKey_ = true;
    }
    lastBacklightOn_ = backlightOn;

    if (restartAtMs_ != 0U && (int32_t)(millis() - restartAtMs_) >= 0) {
        delay(30);
        esp_restart();
    }

    vTaskDelay(pdMS_TO_TICKS(25));
}
