#pragma once
/**
 * @file WifiModule.h
 * @brief WiFi connectivity module.
 */
#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include <WiFi.h>

/** @brief WiFi configuration values. */
struct WifiConfig {
    bool enabled = true;
    char ssid[32] = "CasaParigi";
    char pass[64] = "Elsa2011Andrea2017Clara2019";
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

    /** @brief Depends on log hub and datastore. */
    uint8_t dependencyCount() const override { return 2; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        return nullptr;
    }

    /** @brief Initialize WiFi config/services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief WiFi task loop. */
    void loop() override;

private:
    WifiConfig cfgData;
    WifiState state = WifiState::Idle;
    uint32_t stateTs = 0;
    const LogHubService* logHub = nullptr;
    DataStore* dataStore = nullptr;
    bool gotIpSent = false;
    
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

    // service
    static WifiState svcState(void* ctx);
    static bool svcIsConnected(void* ctx);
    static bool svcGetIP(void* ctx, char* out, size_t len);

    void setState(WifiState s);
    void startConnect();
};
