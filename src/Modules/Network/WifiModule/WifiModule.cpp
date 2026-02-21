/**
 * @file WifiModule.cpp
 * @brief Implementation file.
 */
#include "WifiModule.h"
#define LOG_TAG "WifiModu"
#include "Core/ModuleLog.h"
#include "Core/Runtime.h"
#include <ArduinoJson.h>
#include <esp_wifi.h>
#include <esp_err.h>
#include <ctype.h>
#include <string.h>

namespace {
WifiModule* gWifiModuleInstance = nullptr;

const char* espErrName_(esp_err_t err)
{
    const char* n = esp_err_to_name(err);
    return n ? n : "?";
}
}

WifiState WifiModule::svcState(void* ctx) {
    WifiModule* self = (WifiModule*)ctx;
    return self->state;
}

bool WifiModule::svcIsConnected(void* ctx) {
    (void)ctx;
    return WiFi.isConnected();
}

bool WifiModule::svcGetIP(void* ctx, char* out, size_t len) {
    (void)ctx;
    if (!out || len == 0) return false;

    if (!WiFi.isConnected()) {
        out[0] = '\0';
        return false;
    }

    IPAddress ip = WiFi.localIP();
    snprintf(out, len, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    return true;
}

bool WifiModule::svcRequestReconnect(void* ctx)
{
    WifiModule* self = static_cast<WifiModule*>(ctx);
    if (!self) return false;

    self->stopMdns_();
    self->gotIpSent = false;
    if (self->dataStore) {
        setWifiReady(*self->dataStore, false);
    }

    WiFi.disconnect(false, false);
    self->setState(WifiState::Idle);
    return true;
}

bool WifiModule::svcRequestScan(void* ctx, bool force)
{
    WifiModule* self = static_cast<WifiModule*>(ctx);
    if (!self) return false;
    return self->requestScan_(force);
}

bool WifiModule::svcScanStatusJson(void* ctx, char* out, size_t outLen)
{
    WifiModule* self = static_cast<WifiModule*>(ctx);
    if (!self) return false;
    return self->buildScanStatusJson_(out, outLen);
}

void WifiModule::onWifiEventSys_(arduino_event_t* event)
{
    if (!event) return;
    WifiModule* self = gWifiModuleInstance;
    if (!self) return;

    switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const uint8_t reason = (uint8_t)event->event_info.wifi_sta_disconnected.reason;
        self->lastDisconnectReason_ = reason;
        const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)reason);
        LOGW("STA disconnected reason=%u(%s)", (unsigned)reason, reasonName ? reasonName : "?");
        break;
    }
    default:
        break;
    }
}

const char* WifiModule::wlStatusName_(wl_status_t st)
{
    switch (st) {
    case WL_NO_SHIELD: return "NO_SHIELD";
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID_AVAIL";
    case WL_SCAN_COMPLETED: return "SCAN_COMPLETED";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
    }
}

const char* WifiModule::stateName_(WifiState s)
{
    switch (s) {
    case WifiState::Disabled: return "Disabled";
    case WifiState::Idle: return "Idle";
    case WifiState::Connecting: return "Connecting";
    case WifiState::Connected: return "Connected";
    case WifiState::ErrorWait: return "ErrorWait";
    default: return "Unknown";
    }
}

bool WifiModule::cmdDumpCfg_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen)
{
    (void)req;
    WifiModule* self = static_cast<WifiModule*>(userCtx);
    if (!self || !reply || replyLen == 0) return false;

    const size_t ssidLen = strnlen(self->cfgData.ssid, sizeof(self->cfgData.ssid));
    const size_t passLen = strnlen(self->cfgData.pass, sizeof(self->cfgData.pass));
    const size_t mdnsLen = strnlen(self->cfgData.mdns, sizeof(self->cfgData.mdns));

    StaticJsonDocument<512> doc;
    doc["ok"] = true;
    doc["enabled"] = self->cfgData.enabled;
    doc["state"] = stateName_(self->state);
    doc["wl_status"] = wlStatusName_(WiFi.status());
    doc["ssid"] = self->cfgData.ssid;
    doc["ssid_len"] = (uint32_t)ssidLen;
    doc["pass"] = self->cfgData.pass;
    doc["pass_len"] = (uint32_t)passLen;
    doc["mdns"] = self->cfgData.mdns;
    doc["mdns_len"] = (uint32_t)mdnsLen;
    doc["connected"] = WiFi.isConnected();
    doc["rssi"] = WiFi.isConnected() ? WiFi.RSSI() : -127;
    doc["last_disconnect_reason"] = (uint32_t)self->lastDisconnectReason_;
    const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)self->lastDisconnectReason_);
    doc["last_disconnect_reason_name"] = reasonName ? reasonName : "?";

    IPAddress ip = WiFi.localIP();
    char ipText[16] = {0};
    snprintf(ipText, sizeof(ipText), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    doc["ip"] = ipText;

    const size_t written = serializeJson(doc, reply, replyLen);
    return written > 0 && written < replyLen;
}

void WifiModule::logConfigSummary_() const
{
    const size_t ssidLen = strnlen(cfgData.ssid, sizeof(cfgData.ssid));
    const size_t passLen = strnlen(cfgData.pass, sizeof(cfgData.pass));
    const size_t mdnsLen = strnlen(cfgData.mdns, sizeof(cfgData.mdns));

    if (ssidLen == 0U) {
        LOGW("WiFi config loaded enabled=%d ssid=<empty> pass_len=%u mdns='%s' mdns_len=%u",
             (int)cfgData.enabled,
             (unsigned)passLen,
             cfgData.mdns,
             (unsigned)mdnsLen);
        return;
    }

    LOGI("WiFi config loaded enabled=%d ssid='%s' ssid_len=%u pass_len=%u mdns='%s' mdns_len=%u",
         (int)cfgData.enabled,
         cfgData.ssid,
         (unsigned)ssidLen,
         (unsigned)passLen,
         cfgData.mdns,
         (unsigned)mdnsLen);
}

bool WifiModule::startConnectFallback_()
{
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));

    if (!WiFi.enableSTA(true)) {
        LOGE("Fallback enableSTA failed");
        return false;
    }

    const size_t ssidLen = strnlen(cfgData.ssid, sizeof(cfgData.ssid));
    const size_t passLen = strnlen(cfgData.pass, sizeof(cfgData.pass));
    if (ssidLen == 0U || ssidLen > 32U || passLen > 64U) {
        LOGE("Fallback connect aborted invalid lens ssid=%u pass=%u",
             (unsigned)ssidLen,
             (unsigned)passLen);
        return false;
    }

    memcpy(conf.sta.ssid, cfgData.ssid, ssidLen);
    conf.sta.ssid[ssidLen] = '\0';
    if (passLen > 0U) {
        memcpy(conf.sta.password, cfgData.pass, passLen);
        conf.sta.password[passLen] = '\0';
        conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
    } else {
        conf.sta.password[0] = '\0';
        conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }
    conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    conf.sta.pmf_cfg.capable = true;
    conf.sta.pmf_cfg.required = false;
    conf.sta.bssid_set = 0;

    const esp_err_t derr = esp_wifi_disconnect();
    if (derr != ESP_OK && derr != ESP_ERR_WIFI_NOT_CONNECT) {
        LOGW("Fallback esp_wifi_disconnect failed err=%s(%d)", espErrName_(derr), (int)derr);
    }

    const esp_err_t serr = esp_wifi_set_config((wifi_interface_t)ESP_IF_WIFI_STA, &conf);
    if (serr != ESP_OK) {
        LOGE("Fallback esp_wifi_set_config failed err=%s(%d)", espErrName_(serr), (int)serr);
        return false;
    }

    const esp_err_t cerr = esp_wifi_connect();
    if (cerr != ESP_OK) {
        LOGE("Fallback esp_wifi_connect failed err=%s(%d)", espErrName_(cerr), (int)cerr);
        return false;
    }

    LOGW("Fallback connect path armed (esp_wifi_set_config + esp_wifi_connect)");
    return true;
}

void WifiModule::setState(WifiState s) {
    if (s == state) return;
    state = s;
    stateTs = millis();

    if (state == WifiState::Idle || state == WifiState::ErrorWait || state == WifiState::Disabled) {
        stopMdns_();
        if (dataStore) {
            setWifiReady(*dataStore, false);
        }
        gotIpSent = false;
    }
}

void WifiModule::startConnect() {
    const size_t ssidLen = strnlen(cfgData.ssid, sizeof(cfgData.ssid));
    bool ssidOnlySpaces = true;
    for (size_t i = 0; i < ssidLen; ++i) {
        if (!isspace((unsigned char)cfgData.ssid[i])) {
            ssidOnlySpaces = false;
            break;
        }
    }

    if (ssidLen == 0U || ssidOnlySpaces) {
        const uint32_t now = millis();
        if ((now - lastEmptySsidLogMs) >= 10000U) {
            lastEmptySsidLogMs = now;
            LOGW("SSID empty/blank, skipping connection (enabled=%d)", (int)cfgData.enabled);
        }
        setState(WifiState::Idle);
        return;
    }

    ++connectAttempt_;
    const size_t passLen = strnlen(cfgData.pass, sizeof(cfgData.pass));
    LOGI("Connecting #%lu to ssid='%s' pass_len=%u",
         (unsigned long)connectAttempt_,
         cfgData.ssid,
         (unsigned)passLen);
    reconnectKickSent_ = false;
    lastConnectStatus_ = WL_IDLE_STATUS;
    lastDisconnectReason_ = 0;
    lastConnectingLogMs_ = millis();

    WiFi.disconnect(false, false);
    delay(50);

    if (!WiFi.enableSTA(true)) {
        LOGE("enableSTA failed before connect");
        setState(WifiState::ErrorWait);
        return;
    }

    const wifi_mode_t modeNow = WiFi.getMode();
    const bool keepAp = (modeNow == WIFI_MODE_AP || modeNow == WIFI_MODE_APSTA);
    const wifi_mode_t wantedMode = keepAp ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    const bool modeOk = WiFi.mode(wantedMode);
    if (!modeOk) {
        LOGW("WiFi.mode failed requested=%d current=%d", (int)wantedMode, (int)WiFi.getMode());
    }
    WiFi.setSleep(false);               ///< ✅ important (stability)
    const wl_status_t beginStatus = WiFi.begin(cfgData.ssid, cfgData.pass);
    if (beginStatus == WL_CONNECT_FAILED) {
        LOGW("WiFi.begin returned CONNECT_FAILED for ssid='%s'", cfgData.ssid);
        (void)startConnectFallback_();
    }

    setState(WifiState::Connecting);
}

bool WifiModule::requestScan_(bool force)
{
    const uint32_t now = millis();
    constexpr uint32_t kScanForceMinIntervalMs = 2500U;

    bool running = false;
    bool alreadyRequested = false;
    uint32_t lastDone = 0U;
    portENTER_CRITICAL(&scanMux_);
    running = scanRunning_;
    alreadyRequested = scanRequested_;
    lastDone = scanLastDoneMs_;
    if (!running && !alreadyRequested) {
        if (!force && lastDone != 0U && (now - lastDone) < kScanThrottleMs) {
            portEXIT_CRITICAL(&scanMux_);
            return true;
        }
        if (force && lastDone != 0U && (now - lastDone) < kScanForceMinIntervalMs) {
            portEXIT_CRITICAL(&scanMux_);
            return true;
        }
        scanRequested_ = true;
        scanApRetryCount_ = 0;
    }
    portEXIT_CRITICAL(&scanMux_);
    return true;
}

void WifiModule::processScan_()
{
    if (scanRunning_) {
        const int16_t status = WiFi.scanComplete();
        if (status == WIFI_SCAN_RUNNING) return;

        if (status < 0) {
            portENTER_CRITICAL(&scanMux_);
            scanRunning_ = false;
            scanLastError_ = status;
            scanLastDoneMs_ = millis();
            portEXIT_CRITICAL(&scanMux_);
            WiFi.scanDelete();
            LOGW("WiFi scan failed status=%d", (int)status);
            return;
        }

        WifiScanEntry local[kScanMaxResults] = {};
        uint8_t localCount = 0;
        const int16_t total = status;

        for (int16_t i = 0; i < total; ++i) {
            String ssid = WiFi.SSID(i);
            const bool hidden = (ssid.length() == 0);
            char ssidBuf[33] = {0};
            if (hidden) {
                snprintf(ssidBuf, sizeof(ssidBuf), "<hidden>");
            } else {
                snprintf(ssidBuf, sizeof(ssidBuf), "%s", ssid.c_str());
            }

            const int32_t rssi = WiFi.RSSI(i);
            const uint8_t auth = (uint8_t)WiFi.encryptionType(i);

            int8_t found = -1;
            for (uint8_t j = 0; j < localCount; ++j) {
                if (strcmp(local[j].ssid, ssidBuf) == 0) {
                    found = (int8_t)j;
                    break;
                }
            }

            if (found >= 0) {
                if (rssi > local[(uint8_t)found].rssi) {
                    local[(uint8_t)found].rssi = (int16_t)rssi;
                    local[(uint8_t)found].auth = auth;
                    local[(uint8_t)found].hidden = hidden;
                }
                continue;
            }

            if (localCount >= kScanMaxResults) continue;

            snprintf(local[localCount].ssid, sizeof(local[localCount].ssid), "%s", ssidBuf);
            local[localCount].rssi = (int16_t)rssi;
            local[localCount].auth = auth;
            local[localCount].hidden = hidden;
            ++localCount;
        }

        for (uint8_t i = 0; i < localCount; ++i) {
            for (uint8_t j = (uint8_t)(i + 1U); j < localCount; ++j) {
                if (local[j].rssi > local[i].rssi) {
                    WifiScanEntry tmp = local[i];
                    local[i] = local[j];
                    local[j] = tmp;
                }
            }
        }

        const wifi_mode_t modeAfterScan = WiFi.getMode();
        if (total == 0 && modeAfterScan == WIFI_MODE_APSTA && scanApRetryCount_ == 0U) {
            // In AP+STA mode, a first async scan can sporadically return 0.
            // Retry once with longer channel dwell before concluding "no network".
            ++scanApRetryCount_;
            WiFi.scanDelete();
            const int16_t retryStatus = WiFi.scanNetworks(true, false, false, 500);
            if (retryStatus != WIFI_SCAN_FAILED) {
                portENTER_CRITICAL(&scanMux_);
                scanRunning_ = true;
                scanLastStartMs_ = millis();
                scanLastError_ = 0;
                portEXIT_CRITICAL(&scanMux_);
                LOGW("WiFi scan AP retry started");
                return;
            }
            LOGW("WiFi scan AP retry start failed");
        }

        portENTER_CRITICAL(&scanMux_);
        scanCount_ = localCount;
        scanTotalFound_ = (uint8_t)((total > 255) ? 255 : total);
        for (uint8_t i = 0; i < localCount; ++i) {
            scanEntries_[i] = local[i];
        }
        scanHasResults_ = true;
        scanRunning_ = false;
        scanLastError_ = 0;
        scanLastDoneMs_ = millis();
        ++scanGeneration_;
        portEXIT_CRITICAL(&scanMux_);

        WiFi.scanDelete();
        LOGI("WiFi scan done total=%d kept=%u", (int)total, (unsigned)localCount);
        return;
    }

    if (!scanRequested_) return;

    const wifi_mode_t modeNow = WiFi.getMode();
    if (modeNow == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_MODE_STA);
    } else if (modeNow == WIFI_MODE_AP) {
        WiFi.mode(WIFI_MODE_APSTA);
    }

    scanRequested_ = false;
    const int16_t startStatus = WiFi.scanNetworks(true, false, false, 360);
    if (startStatus == WIFI_SCAN_FAILED) {
        portENTER_CRITICAL(&scanMux_);
        scanRunning_ = false;
        scanLastError_ = WIFI_SCAN_FAILED;
        scanLastDoneMs_ = millis();
        portEXIT_CRITICAL(&scanMux_);
        LOGW("WiFi scan start failed");
        return;
    }

    portENTER_CRITICAL(&scanMux_);
    scanRunning_ = true;
    scanLastStartMs_ = millis();
    scanLastError_ = 0;
    portEXIT_CRITICAL(&scanMux_);
}

bool WifiModule::buildScanStatusJson_(char* out, size_t outLen)
{
    if (!out || outLen == 0) return false;

    WifiScanEntry local[kScanMaxResults] = {};
    bool running = false;
    bool requested = false;
    bool hasResults = false;
    int16_t lastErr = 0;
    uint8_t count = 0;
    uint8_t total = 0;
    uint32_t startedMs = 0;
    uint32_t doneMs = 0;
    uint16_t gen = 0;

    portENTER_CRITICAL(&scanMux_);
    running = scanRunning_;
    requested = scanRequested_;
    hasResults = scanHasResults_;
    lastErr = scanLastError_;
    count = scanCount_;
    total = scanTotalFound_;
    startedMs = scanLastStartMs_;
    doneMs = scanLastDoneMs_;
    gen = scanGeneration_;
    for (uint8_t i = 0; i < count; ++i) {
        local[i] = scanEntries_[i];
    }
    portEXIT_CRITICAL(&scanMux_);

    StaticJsonDocument<3072> doc;
    doc["ok"] = true;
    doc["running"] = running;
    doc["requested"] = requested;
    doc["has_results"] = hasResults;
    doc["count"] = count;
    doc["total_found"] = total;
    doc["generation"] = gen;
    doc["last_error"] = lastErr;
    doc["started_ms"] = startedMs;
    doc["updated_ms"] = doneMs;

    JsonArray nets = doc.createNestedArray("networks");
    for (uint8_t i = 0; i < count; ++i) {
        JsonObject n = nets.createNestedObject();
        n["ssid"] = local[i].ssid;
        n["rssi"] = local[i].rssi;
        n["auth"] = local[i].auth;
        n["secure"] = (local[i].auth != WIFI_AUTH_OPEN);
        n["hidden"] = local[i].hidden;
    }

    const size_t wrote = serializeJson(doc, out, outLen);
    return wrote > 0 && wrote < outLen;
}

void WifiModule::init(ConfigStore& cfg,
                      ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Wifi;
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::Wifi;
    /// récupérer service loghub (log async)
    logHub = services.get<LogHubService>("loghub");

    const DataStoreService* dsSvc = services.get<DataStoreService>("datastore");
    dataStore = dsSvc ? dsSvc->store : nullptr;

    /// Register config vars
    cfg.registerVar(enabledVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(ssidVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(passVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(mdnsVar, kCfgModuleId, kCfgBranchId);

    /// Register WifiService
    static WifiService svc {
        WifiModule::svcState,
        WifiModule::svcIsConnected,
        WifiModule::svcGetIP,
        this,
        WifiModule::svcRequestReconnect,
        WifiModule::svcRequestScan,
        WifiModule::svcScanStatusJson
    };
    services.add("wifi", &svc);

    const CommandService* cmdSvc = services.get<CommandService>("cmd");
    if (cmdSvc && cmdSvc->registerHandler) {
        const bool ok = cmdSvc->registerHandler(cmdSvc->ctx, "wifi.dump_cfg", &WifiModule::cmdDumpCfg_, this);
        if (!ok) {
            LOGW("wifi.dump_cfg registration failed");
        } else {
            LOGI("Command registered: wifi.dump_cfg");
        }
    } else {
        LOGW("Command service unavailable: wifi.dump_cfg not registered");
    }

    // Keep WiFi credentials managed by ConfigStore only (no duplicate driver persistence).
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);
    gWifiModuleInstance = this;
    if (wifiEventHandlerId_ != 0U) {
        WiFi.removeEvent(wifiEventHandlerId_);
        wifiEventHandlerId_ = 0U;
    }
    wifiEventHandlerId_ = WiFi.onEvent(WifiModule::onWifiEventSys_);

    LOGI("WifiService registered");
    setState(WifiState::Idle);
}

void WifiModule::onConfigLoaded(ConfigStore&, ServiceRegistry&)
{
    logConfigSummary_();
    if (!cfgData.enabled) {
        LOGW("WiFi disabled in config, disconnecting STA");
        WiFi.disconnect(false, false);
        setState(WifiState::Disabled);
        return;
    }
    setState(WifiState::Idle);
}

void WifiModule::stopMdns_()
{
    if (!mdnsStarted) return;
    MDNS.end();
    mdnsStarted = false;
    mdnsApplied[0] = '\0';
    LOGI("mDNS stopped");
}

void WifiModule::syncMdns_()
{
    if (!WiFi.isConnected()) {
        stopMdns_();
        return;
    }

    char host[sizeof(cfgData.mdns)] = {0};
    size_t w = 0;
    for (size_t i = 0; cfgData.mdns[i] != '\0' && w < (sizeof(host) - 1); ++i) {
        char c = cfgData.mdns[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            host[w++] = (char)tolower((unsigned char)c);
        } else if (c == ' ' || c == '_' || c == '.') {
            host[w++] = '-';
        }
    }
    host[w] = '\0';

    while (w > 0 && host[0] == '-') {
        memmove(host, host + 1, w);
        --w;
    }
    while (w > 0 && host[w - 1] == '-') {
        host[w - 1] = '\0';
        --w;
    }

    if (host[0] == '\0') {
        stopMdns_();
        return;
    }

    if (mdnsStarted && strcmp(mdnsApplied, host) == 0) return;

    if (mdnsStarted) {
        MDNS.end();
        mdnsStarted = false;
        mdnsApplied[0] = '\0';
    }

    if (!MDNS.begin(host)) {
        LOGW("mDNS start failed host=%s", host);
        return;
    }

    mdnsStarted = true;
    snprintf(mdnsApplied, sizeof(mdnsApplied), "%s", host);
    LOGI("mDNS started host=%s.local", mdnsApplied);
}

void WifiModule::loop() {
    processScan_();

    switch (state) {

    case WifiState::Disabled:
        vTaskDelay(pdMS_TO_TICKS(2000));
        break;

    case WifiState::Idle:
        startConnect();
        vTaskDelay(pdMS_TO_TICKS(1000));
        break;

    case WifiState::Connecting:
    {
        const wl_status_t wl = WiFi.status();
        const uint32_t now = millis();
        if (wl != lastConnectStatus_) {
            lastConnectStatus_ = wl;
        }

        if ((now - lastConnectingLogMs_) >= 3000U) {
            lastConnectingLogMs_ = now;
            const int rssi = WiFi.isConnected() ? WiFi.RSSI() : -127;
            const char* wlName = wlStatusName_(wl);
            const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)lastDisconnectReason_);
            LOGI("Connecting status=%s(%d) rssi=%d last_reason=%u(%s) elapsed_ms=%lu",
                 wlName,
                 (int)wl,
                 rssi,
                 (unsigned)lastDisconnectReason_,
                 reasonName ? reasonName : "?",
                 (unsigned long)(now - stateTs));
        }

        if (!reconnectKickSent_ && (now - stateTs) > 4000U && wl == WL_DISCONNECTED) {
            reconnectKickSent_ = true;
            WiFi.reconnect();
        }

        if (WiFi.isConnected()) {
            IPAddress ip = WiFi.localIP();
            LOGI("Connected IP=%u.%u.%u.%u RSSI=%d",
            ip[0], ip[1], ip[2], ip[3],
            WiFi.RSSI());
            setState(WifiState::Connected);
        }
        else if (now - stateTs > 15000) {
            const char* wlName = wlStatusName_(wl);
            if (lastDisconnectReason_ != 0U) {
                const char* reasonName = WiFi.disconnectReasonName((wifi_err_reason_t)lastDisconnectReason_);
                LOGW("Connect timeout status=%s(%d) reason=%u(%s)",
                     wlName,
                     (int)wl,
                     (unsigned)lastDisconnectReason_,
                     reasonName ? reasonName : "?");
            } else {
                LOGW("Connect timeout status=%s(%d)", wlName, (int)wl);
            }
            WiFi.disconnect(false, false);
            setState(WifiState::ErrorWait);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        break;
    }

    case WifiState::Connected:
        if (!WiFi.isConnected()) {
            LOGW("Disconnected");
            setState(WifiState::ErrorWait);
        }
        if (state == WifiState::Connected) {
            syncMdns_();
        }
        if (state == WifiState::Connected && !gotIpSent) {
            IPAddress ip = WiFi.localIP();
            if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0) {
                if (dataStore) {
                    IpV4 ip4{};
                    ip4.b[0] = ip[0];
                    ip4.b[1] = ip[1];
                    ip4.b[2] = ip[2];
                    ip4.b[3] = ip[3];

                    setWifiIp(*dataStore, ip4);
                    setWifiReady(*dataStore, true);
                }
                gotIpSent = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        break;

    case WifiState::ErrorWait:
        if (millis() - stateTs > 5000) {
            setState(WifiState::Idle);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        break;
    }
}
