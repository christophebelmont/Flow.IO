#pragma once
/**
 * @file WifiModule.h
 * @brief WiFi connectivity module.
 */
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include <WiFi.h>
#include <ESPmDNS.h>

/** @brief WiFi configuration values. */
struct WifiConfig {
    bool enabled = true;
    // IEEE 802.11 SSID supports up to 32 bytes (+ '\0').
    char ssid[33] = "";
    // WPA/WPA2 supports 8..63 chars passphrase or 64-char hex PSK (+ '\0').
    char pass[65] = "";
    char mdns[33] = "flowio";
};

/**
 * @brief Active module that manages WiFi connectivity.
 */
class WifiModule : public Module {
public:
    /** @brief Module id. */
    const char* moduleId() const override { return "wifi"; }
    /** @brief Task name. */
    const char* taskName() const override { return "wifi"; }
    /** @brief Pin network module on core 0. */
    BaseType_t taskCore() const override { return 0; }
    /** @brief Give extra headroom to WiFi stack/callback activity. */
    uint16_t taskStackSize() const override { return 4096; }

    /** @brief Depends on log hub and datastore. */
    uint8_t dependencyCount() const override { return 2; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        return nullptr;
    }

    /** @brief Initialize WiFi config/services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Apply loaded persistent config. */
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief WiFi task loop. */
    void loop() override;

private:
    static constexpr uint8_t kScanMaxResults = 24;
    static constexpr uint32_t kScanThrottleMs = 8000U;

    struct WifiScanEntry {
        char ssid[33];
        int16_t rssi;
        uint8_t auth;
        bool hidden;
    };

    WifiConfig cfgData;
    WifiState state = WifiState::Idle;
    uint32_t stateTs = 0;
    const LogHubService* logHub = nullptr;
    DataStore* dataStore = nullptr;
    bool gotIpSent = false;
    bool mdnsStarted = false;
    uint32_t connectAttempt_ = 0;
    bool reconnectKickSent_ = false;
    wl_status_t lastConnectStatus_ = WL_IDLE_STATUS;
    uint8_t lastDisconnectReason_ = 0;
    uint32_t lastConnectingLogMs_ = 0;
    wifi_event_id_t wifiEventHandlerId_ = 0;
    uint32_t lastEmptySsidLogMs = 0;
    char mdnsApplied[sizeof(cfgData.mdns)] = {0};
    volatile bool scanRequested_ = false;
    volatile bool scanRunning_ = false;
    bool scanHasResults_ = false;
    int16_t scanLastError_ = 0;
    uint8_t scanCount_ = 0;
    uint8_t scanTotalFound_ = 0;
    uint8_t scanApRetryCount_ = 0;
    uint32_t scanLastStartMs_ = 0;
    uint32_t scanLastDoneMs_ = 0;
    uint16_t scanGeneration_ = 0;
    WifiScanEntry scanEntries_[kScanMaxResults] = {};
    portMUX_TYPE scanMux_ = portMUX_INITIALIZER_UNLOCKED;
    
    // Config variables
    ConfigVariable<bool,0> enabledVar {
        NVS_KEY(NvsKeys::Wifi::Enabled),"enabled","wifi",
        ConfigType::Bool,
        &cfgData.enabled,
        ConfigPersistence::Persistent,
        0
    };

    ConfigVariable<char,0> ssidVar {
        NVS_KEY(NvsKeys::Wifi::Ssid),"ssid","wifi",
        ConfigType::CharArray,
        cfgData.ssid,
        ConfigPersistence::Persistent,
        sizeof(cfgData.ssid)
    };

    ConfigVariable<char,0> passVar {
        NVS_KEY(NvsKeys::Wifi::Pass),"pass","wifi",
        ConfigType::CharArray,
        cfgData.pass,
        ConfigPersistence::Persistent,
        sizeof(cfgData.pass)
    };

    ConfigVariable<char,0> mdnsVar {
        NVS_KEY(NvsKeys::Wifi::Mdns),"mdns","wifi",
        ConfigType::CharArray,
        cfgData.mdns,
        ConfigPersistence::Persistent,
        sizeof(cfgData.mdns)
    };

    // service
    static WifiState svcState(void* ctx);
    static bool svcIsConnected(void* ctx);
    static bool svcGetIP(void* ctx, char* out, size_t len);
    static bool svcRequestReconnect(void* ctx);
    static bool svcRequestScan(void* ctx, bool force);
    static bool svcScanStatusJson(void* ctx, char* out, size_t outLen);
    static bool cmdDumpCfg_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    void setState(WifiState s);
    static void onWifiEventSys_(arduino_event_t* event);
    static const char* stateName_(WifiState s);
    static const char* wlStatusName_(wl_status_t st);
    void logConfigSummary_() const;
    bool startConnectFallback_();
    void startConnect();
    void stopMdns_();
    void syncMdns_();
    bool requestScan_(bool force);
    void processScan_();
    bool buildScanStatusJson_(char* out, size_t outLen);
};
