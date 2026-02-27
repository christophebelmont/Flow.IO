/**
 * @file WifiProvisioningModule.cpp
 * @brief Supervisor-only WiFi provisioning overlay implementation.
 */

#include "WifiProvisioningModule.h"

#define LOG_TAG "WifiProv"
#include "Core/ModuleLog.h"

#include <ArduinoJson.h>
#include <WiFi.h>

namespace {
constexpr const char* kDefaultApPass = "flowio1234";
WifiProvisioningModule* gWifiProvisioningInstance = nullptr;
}

void WifiProvisioningModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfgStore_ = &cfg;
    wifiSvc_ = services.get<WifiService>("wifi");
    bootMs_ = millis();
    lastCfgPollMs_ = 0;
    buildApCredentials_();

    static NetworkAccessService netSvc{
        &WifiProvisioningModule::svcIsWebReachable_,
        &WifiProvisioningModule::svcMode_,
        &WifiProvisioningModule::svcGetIP_,
        &WifiProvisioningModule::svcNotifyWifiConfigChanged_,
        nullptr
    };
    netSvc.ctx = this;
    services.add("network_access", &netSvc);

    gWifiProvisioningInstance = this;
    if (wifiEventHandlerId_ != 0U) {
        WiFi.removeEvent(wifiEventHandlerId_);
        wifiEventHandlerId_ = 0U;
    }
    wifiEventHandlerId_ = WiFi.onEvent(WifiProvisioningModule::onWifiEventSys_);

    LOGI("Provisioning overlay initialized (timeout=%lu ms, AP SSID=%s)",
         (unsigned long)kConnectTimeoutMs,
         apSsid_);
}

void WifiProvisioningModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    refreshWifiConfig_();
    LOGI("Provisioning config loaded: enabled=%d configured=%d",
         (int)wifiEnabled_,
         (int)wifiConfigured_);
    ensurePortalStarted_();
}

void WifiProvisioningModule::loop()
{
    const uint32_t now = millis();
    if (configDirty_ || (now - lastCfgPollMs_) >= kConfigPollMs) {
        lastCfgPollMs_ = now;
        configDirty_ = false;
        refreshWifiConfig_();
    }

    const bool staConnected = isStaConnected_();
    if (staConnected) {
        if (apActive_) {
            stopCaptivePortal_();
        }
        portalLatched_ = false;
        vTaskDelay(pdMS_TO_TICKS(250));
        return;
    }

    ensurePortalStarted_();

    if (apActive_) {
        handleStaProbePolicy_(now);
        dns_.processNextRequest();
    }

    vTaskDelay(pdMS_TO_TICKS(20));
}

void WifiProvisioningModule::ensurePortalStarted_()
{
    if (apActive_ || portalLatched_) return;
    if (isStaConnected_()) return;

    const PortalReason reason = evaluatePortalReason_();
    if (reason == PortalReason::None) return;

    if (startCaptivePortal_(reason)) {
        portalLatched_ = true;
    }
}

bool WifiProvisioningModule::svcIsWebReachable_(void* ctx)
{
    WifiProvisioningModule* self = static_cast<WifiProvisioningModule*>(ctx);
    if (!self) return false;
    return self->isStaConnected_() || self->apActive_;
}

NetworkAccessMode WifiProvisioningModule::svcMode_(void* ctx)
{
    WifiProvisioningModule* self = static_cast<WifiProvisioningModule*>(ctx);
    if (!self) return NetworkAccessMode::None;
    if (self->isStaConnected_()) return NetworkAccessMode::Station;
    if (self->apActive_) return NetworkAccessMode::AccessPoint;
    return NetworkAccessMode::None;
}

bool WifiProvisioningModule::svcGetIP_(void* ctx, char* out, size_t len)
{
    WifiProvisioningModule* self = static_cast<WifiProvisioningModule*>(ctx);
    if (!self || !out || len == 0) return false;
    if (self->isStaConnected_()) {
        return self->getStaIp_(out, len);
    }
    if (self->apActive_) {
        return self->getApIp_(out, len);
    }
    out[0] = '\0';
    return false;
}

bool WifiProvisioningModule::svcNotifyWifiConfigChanged_(void* ctx)
{
    WifiProvisioningModule* self = static_cast<WifiProvisioningModule*>(ctx);
    if (!self) return false;

    self->configDirty_ = true;
    if (self->wifiSvc_ && self->wifiSvc_->setStaRetryEnabled) {
        (void)self->wifiSvc_->setStaRetryEnabled(self->wifiSvc_->ctx, true);
    }
    if (self->wifiSvc_ && self->wifiSvc_->requestReconnect) {
        self->wifiSvc_->requestReconnect(self->wifiSvc_->ctx);
    }
    return true;
}

void WifiProvisioningModule::buildApCredentials_()
{
    const uint64_t chipId = ESP.getEfuseMac();
    const uint8_t b0 = (uint8_t)(chipId >> 16);
    const uint8_t b1 = (uint8_t)(chipId >> 8);
    const uint8_t b2 = (uint8_t)(chipId >> 0);
    snprintf(apSsid_, sizeof(apSsid_), "FlowIO-Supervisor-%02X%02X%02X", b0, b1, b2);
    snprintf(apPass_, sizeof(apPass_), "%s", kDefaultApPass);
}

void WifiProvisioningModule::refreshWifiConfig_()
{
    if (!cfgStore_) return;

    char wifiJson[320] = {0};
    if (!cfgStore_->toJsonModule("wifi", wifiJson, sizeof(wifiJson), nullptr)) {
        wifiConfigured_ = false;
        wifiEnabled_ = true;
        return;
    }

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, wifiJson);
    if (err || !doc.is<JsonObjectConst>()) {
        LOGW("Cannot parse wifi config for provisioning");
        wifiConfigured_ = false;
        wifiEnabled_ = true;
        return;
    }

    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* ssid = root["ssid"] | "";
    wifiEnabled_ = root["enabled"] | true;
    wifiConfigured_ = wifiEnabled_ && ssid && ssid[0] != '\0';
}

WifiProvisioningModule::PortalReason WifiProvisioningModule::evaluatePortalReason_() const
{
    if (!wifiConfigured_) {
        return PortalReason::MissingCredentials;
    }
    if ((millis() - bootMs_) >= kConnectTimeoutMs) {
        return PortalReason::ConnectTimeout;
    }
    return PortalReason::None;
}

bool WifiProvisioningModule::startCaptivePortal_(PortalReason reason)
{
    if (apActive_) return true;

    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, false);
    }

    WiFi.mode(WIFI_MODE_AP);
    const bool ok = WiFi.softAP(apSsid_, apPass_);
    if (!ok) {
        LOGE("Cannot start AP portal");
        return false;
    }

    const IPAddress apIp = WiFi.softAPIP();
    dns_.start(kDnsPort, "*", apIp);
    apActive_ = true;
    staProbeActive_ = false;
    lastStaProbeStartMs_ = millis();
    refreshApClientState_(lastStaProbeStartMs_, false);

    const char* reasonTxt = (reason == PortalReason::MissingCredentials) ? "missing credentials" : "connect timeout";
    LOGW("Provisioning AP started (%s) SSID=%s IP=%u.%u.%u.%u",
         reasonTxt,
         apSsid_,
         apIp[0], apIp[1], apIp[2], apIp[3]);
    return true;
}

void WifiProvisioningModule::stopCaptivePortal_()
{
    if (!apActive_) return;

    stopStaProbe_("sta connected");
    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, true);
    }
    dns_.stop();
    WiFi.softAPdisconnect(true);
    apActive_ = false;
    apClientCount_ = 0;
    lastApClientSeenMs_ = 0;
    lastApClientPollMs_ = 0;
    LOGI("Provisioning AP stopped (STA connected)");
}

void WifiProvisioningModule::onWifiEventSys_(arduino_event_t* event)
{
    if (!event) return;
    WifiProvisioningModule* self = gWifiProvisioningInstance;
    if (!self) return;
    self->onWifiEvent_(event);
}

void WifiProvisioningModule::onWifiEvent_(arduino_event_t* event)
{
    if (!event) return;
    switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
        refreshApClientState_(millis(), true);
        break;
    default:
        break;
    }
}

void WifiProvisioningModule::refreshApClientState_(uint32_t nowMs, bool fromEvent)
{
    if (!apActive_) {
        apClientCount_ = 0;
        return;
    }

    const uint8_t count = WiFi.softAPgetStationNum();
    if (count > 0) {
        lastApClientSeenMs_ = nowMs;
    }
    if (count != apClientCount_) {
        apClientCount_ = count;
        if (fromEvent) {
            LOGI("AP clients changed (event) count=%u", (unsigned)apClientCount_);
        } else {
            LOGI("AP clients changed (poll) count=%u", (unsigned)apClientCount_);
        }
    }
}

void WifiProvisioningModule::startStaProbe_(uint32_t nowMs)
{
    if (staProbeActive_) return;
    if (!wifiEnabled_ || !wifiConfigured_) return;

    staProbeActive_ = true;
    lastStaProbeStartMs_ = nowMs;
    if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
        (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, true);
    }
    if (wifiSvc_ && wifiSvc_->requestReconnect) {
        (void)wifiSvc_->requestReconnect(wifiSvc_->ctx);
    }
    LOGI("STA probe started window_ms=%lu ap_clients=%u",
         (unsigned long)kStaProbeWindowMs,
         (unsigned)apClientCount_);
}

void WifiProvisioningModule::stopStaProbe_(const char* reason)
{
    if (!staProbeActive_) return;
    staProbeActive_ = false;

    if (!isStaConnected_()) {
        if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
            (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, false);
        }
        if (apActive_ && WiFi.getMode() != WIFI_MODE_AP) {
            (void)WiFi.mode(WIFI_MODE_AP);
        }
    }

    LOGI("STA probe stopped reason=%s", reason ? reason : "unknown");
}

void WifiProvisioningModule::handleStaProbePolicy_(uint32_t nowMs)
{
    if (!apActive_) return;

    if ((nowMs - lastApClientPollMs_) >= kApClientPollMs) {
        lastApClientPollMs_ = nowMs;
        refreshApClientState_(nowMs, false);
    }

    const bool clientConnected = (apClientCount_ > 0U);
    const bool clientSeenRecently = (lastApClientSeenMs_ != 0U) &&
                                    ((nowMs - lastApClientSeenMs_) < kApClientGraceMs);
    const bool holdApOnly = clientConnected || clientSeenRecently || !wifiEnabled_ || !wifiConfigured_;

    if (staProbeActive_) {
        if (holdApOnly) {
            stopStaProbe_(clientConnected ? "ap client connected" : "ap client grace");
            return;
        }
        if ((nowMs - lastStaProbeStartMs_) >= kStaProbeWindowMs) {
            stopStaProbe_("window elapsed");
        }
        return;
    }

    if (holdApOnly) {
        if (wifiSvc_ && wifiSvc_->setStaRetryEnabled) {
            (void)wifiSvc_->setStaRetryEnabled(wifiSvc_->ctx, false);
        }
        return;
    }

    if ((nowMs - lastStaProbeStartMs_) >= kStaProbeIntervalMs) {
        startStaProbe_(nowMs);
    }
}

bool WifiProvisioningModule::isStaConnected_() const
{
    if (!wifiSvc_ || !wifiSvc_->isConnected) return false;
    return wifiSvc_->isConnected(wifiSvc_->ctx);
}

bool WifiProvisioningModule::getStaIp_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    if (!wifiSvc_ || !wifiSvc_->getIP) {
        out[0] = '\0';
        return false;
    }
    return wifiSvc_->getIP(wifiSvc_->ctx, out, len);
}

bool WifiProvisioningModule::getApIp_(char* out, size_t len) const
{
    if (!out || len == 0) return false;
    const IPAddress ip = WiFi.softAPIP();
    if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
        out[0] = '\0';
        return false;
    }
    snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}
