#pragma once
/**
 * @file WifiProvisioningModule.h
 * @brief Supervisor-only WiFi provisioning overlay (STA fallback to AP portal).
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"

#include <DNSServer.h>
#include <WiFi.h>

class WifiProvisioningModule : public Module {
public:
    const char* moduleId() const override { return "wifiprov"; }
    const char* taskName() const override { return "wifiprov"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 4096; }

    uint8_t dependencyCount() const override { return 2; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "wifi";
        if (i == 1) return "loghub";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    enum class PortalReason : uint8_t {
        None = 0,
        MissingCredentials = 1,
        ConnectTimeout = 2
    };

    static constexpr uint32_t kConnectTimeoutMs = 25000U;
    static constexpr uint32_t kConfigPollMs = 1000U;
    static constexpr uint16_t kDnsPort = 53;
    static constexpr uint32_t kApClientPollMs = 1000U;
    static constexpr uint32_t kApClientGraceMs = 120000U;
    static constexpr uint32_t kStaProbeIntervalMs = 30000U;
    static constexpr uint32_t kStaProbeWindowMs = 6000U;

    ConfigStore* cfgStore_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;

    DNSServer dns_;
    bool apActive_ = false;
    bool wifiConfigured_ = false;
    bool wifiEnabled_ = true;
    bool configDirty_ = false;
    bool portalLatched_ = false;
    bool staProbeActive_ = false;
    uint8_t apClientCount_ = 0;
    uint32_t lastApClientSeenMs_ = 0;
    uint32_t lastApClientPollMs_ = 0;
    uint32_t lastStaProbeStartMs_ = 0;
    uint32_t bootMs_ = 0;
    uint32_t lastCfgPollMs_ = 0;
    wifi_event_id_t wifiEventHandlerId_ = 0;
    char apSsid_[40] = {0};
    char apPass_[32] = {0};

    static bool svcIsWebReachable_(void* ctx);
    static NetworkAccessMode svcMode_(void* ctx);
    static bool svcGetIP_(void* ctx, char* out, size_t len);
    static bool svcNotifyWifiConfigChanged_(void* ctx);

    void buildApCredentials_();
    void handleStaProbePolicy_(uint32_t nowMs);
    void refreshApClientState_(uint32_t nowMs, bool fromEvent);
    void startStaProbe_(uint32_t nowMs);
    void stopStaProbe_(const char* reason);
    void refreshWifiConfig_();
    PortalReason evaluatePortalReason_() const;
    void ensurePortalStarted_();
    static void onWifiEventSys_(arduino_event_t* event);
    void onWifiEvent_(arduino_event_t* event);
    bool startCaptivePortal_(PortalReason reason);
    void stopCaptivePortal_();
    bool isStaConnected_() const;
    bool getStaIp_(char* out, size_t len) const;
    bool getApIp_(char* out, size_t len) const;
};
