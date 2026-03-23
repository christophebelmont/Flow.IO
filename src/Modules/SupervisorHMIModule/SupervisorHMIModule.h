#pragma once
/**
 * @file SupervisorHMIModule.h
 * @brief Local Supervisor HMI module (ST7789 + PIR + WiFi reset button).
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Modules/SupervisorHMIModule/Drivers/St7789SupervisorDriver.h"

struct BoardSpec;
struct SupervisorRuntimeOptions;

class SupervisorHMIModule : public Module {
public:
    SupervisorHMIModule(const BoardSpec& board, const SupervisorRuntimeOptions& runtime);

    ModuleId moduleId() const override { return ModuleId::SupervisorHmi; }
    const char* taskName() const override { return "hmi.sup"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 6144; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 6; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::Wifi;
        if (i == 3) return ModuleId::WifiProvisioning;
        if (i == 4) return ModuleId::FirmwareUpdate;
        if (i == 5) return ModuleId::I2cCfgClient;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static St7789SupervisorDriverConfig makeDriverConfig_(const BoardSpec& board);
    static void copyText_(char* out, size_t outLen, const char* in);
    uint32_t buildRenderKey_() const;
    uint32_t currentClockMinute_() const;

    void pollWifiAndNetwork_();
    void pollFirmwareStatus_();
    void pollFlowStatus_();
    void updateBacklight_();
    void updateWifiResetButton_();
    void triggerWifiReset_();
    void rebuildBanner_();
    void setDefaultBanner_();

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const NetworkAccessService* netAccessSvc_ = nullptr;
    const FirmwareUpdateService* fwUpdateSvc_ = nullptr;
    const FlowCfgRemoteService* flowCfgSvc_ = nullptr;

    St7789SupervisorDriverConfig driverCfg_{};
    St7789SupervisorDriver driver_;
    SupervisorHmiViewModel view_{};
    bool driverReady_ = false;

    int8_t pirPin_ = -1;
    int8_t wifiResetPin_ = -1;
    uint32_t pirTimeoutMs_ = 120000U;
    uint32_t pirDebounceMs_ = 120U;
    bool pirActiveHigh_ = true;
    uint32_t wifiResetHoldMs_ = 3000U;
    uint32_t wifiResetDebounceMs_ = 40U;

    bool fwBusyOrPending_ = false;
    bool wifiResetPending_ = false;
    uint32_t restartAtMs_ = 0;

    uint32_t lastFwPollMs_ = 0;
    uint32_t lastRenderMs_ = 0;
    uint32_t splashHoldUntilMs_ = 0;
    uint32_t lastRenderKey_ = 0;
    uint32_t lastRenderedMinute_ = 0;
    bool hasLastRenderKey_ = false;
    bool lastBacklightOn_ = false;
    char flowStatusScratchBuf_[448] = {0};

    bool buttonPressed_ = false;
    bool buttonTriggered_ = false;
    bool buttonArmed_ = false;
    bool buttonRawPressed_ = false;
    bool buttonStablePressed_ = false;
    uint32_t buttonPressedAtMs_ = 0;
    uint32_t buttonHighSinceMs_ = 0;
    uint32_t buttonDebounceChangedAtMs_ = 0;

    bool pirRawState_ = false;
    bool pirStableState_ = false;
    uint32_t pirDebounceChangedAtMs_ = 0;
    uint32_t lastMotionMs_ = 0;
};
