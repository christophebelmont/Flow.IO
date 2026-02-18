#pragma once
/**
 * @file HMIModule.h
 * @brief UI orchestration module (menu model + HMI driver).
 */

#include "Core/Module.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Services/Services.h"
#include "Modules/HMIModule/ConfigMenuModel.h"
#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"
#include "Modules/HMIModule/Drivers/NextionDriver.h"

class HMIModule : public Module {
public:
    const char* moduleId() const override { return "hmi"; }
    const char* taskName() const override { return "HMI"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 6144; }

    uint8_t dependencyCount() const override { return 3; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "config";
        if (i == 2) return "eventbus";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;

    ConfigMenuModel menu_;
    NextionDriver nextion_;
    IHmiDriver* driver_ = nullptr;

    bool driverReady_ = false;
    bool viewDirty_ = true;
    uint32_t lastRenderMs_ = 0;

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void handleDriverEvent_(const HmiEvent& e);
    bool refreshCurrentModule_();
    bool render_();
    bool buildMenuJson_(char* out, size_t outLen) const;

    static bool svcRequestRefresh_(void* ctx);
    static bool svcOpenConfigHome_(void* ctx);
    static bool svcOpenConfigModule_(void* ctx, const char* module);
    static bool svcBuildConfigMenuJson_(void* ctx, char* out, size_t outLen);
};
