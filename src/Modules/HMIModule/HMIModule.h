#pragma once
/**
 * @file HMIModule.h
 * @brief UI orchestration module (menu model + HMI driver).
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/ServiceBinding.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Services/Services.h"
#include "Modules/HMIModule/ConfigMenuModel.h"
#include "Modules/HMIModule/Drivers/HmiDriverTypes.h"
#include "Modules/HMIModule/Drivers/NextionDriver.h"
#include "Modules/HMIModule/Drivers/TfaVeniceRf433Sink.h"

class HMIModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::Hmi; }
    const char* taskName() const override { return "HMI"; }
    BaseType_t taskCore() const override { return 1; }
    uint16_t taskStackSize() const override { return 4096; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 6; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::EventBus;
        if (i == 3) return ModuleId::DataStore;
        if (i == 4) return ModuleId::Io;
        if (i == 5) return ModuleId::Alarm;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    struct ConfigData {
        bool ledsEnabled = true;
        bool nextionEnabled = true;
        bool veniceEnabled = false;
        int32_t veniceTxGpio = 14;
    } cfgData_{};

    // CFGDOC: {"label":"Pilotage LEDs facade", "help":"Autorise le HMIModule a ecrire le masque logique des LEDs via StatusLedsService."}
    ConfigVariable<bool,0> ledsEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::LedsEnabled), "enabled", "hmi/leds",
        ConfigType::Bool, &cfgData_.ledsEnabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Nextion actif", "help":"Autorise le HMIModule a envoyer le rendu et les commandes vers l'ecran Nextion local."}
    ConfigVariable<bool,0> nextionEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::NextionEnabled), "enabled", "hmi/nextion",
        ConfigType::Bool, &cfgData_.nextionEnabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Venice RF433 actif", "help":"Active l'emission periodique de la temperature d'eau vers un afficheur TFA Venice compatible."}
    ConfigVariable<bool,0> veniceEnabledVar_{
        NVS_KEY(NvsKeys::Hmi::VeniceEnabled), "enabled", "hmi/venice",
        ConfigType::Bool, &cfgData_.veniceEnabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO emission Venice", "help":"GPIO utilise pour l'emetteur RF433 du driver Venice."}
    ConfigVariable<int32_t,0> veniceTxGpioVar_{
        NVS_KEY(NvsKeys::Hmi::VeniceTxGpio), "tx_gpio", "hmi/venice",
        ConfigType::Int32, &cfgData_.veniceTxGpio, ConfigPersistence::Persistent, 0
    };
    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const StatusLedsService* statusLedsSvc_ = nullptr;
    EventBus* eventBus_ = nullptr;

    ConfigMenuModel menu_;
    NextionDriver nextion_;
    TfaVeniceRf433Sink venice_;
    IHmiDriver* driver_ = nullptr;

    bool driverReady_ = false;
    bool viewDirty_ = true;
    uint32_t lastRenderMs_ = 0;
    uint8_t ledPage_ = 1;
    uint8_t ledMaskLast_ = 0;
    bool ledMaskValid_ = false;
    bool wifiReady_ = false;
    bool mqttReady_ = false;
    bool autoRegEnabled_ = false;
    bool winterMode_ = false;
    bool phPidEnabled_ = false;
    bool chlorinePidEnabled_ = false;
    bool phTankLowAlarm_ = false;
    bool chlorineTankLowAlarm_ = false;
    bool phPumpRuntimeAlarm_ = false;
    bool chlorinePumpRuntimeAlarm_ = false;
    bool psiAlarm_ = false;
    bool waterLevelLow_ = false;
    bool wifiBlinkOn_ = false;
    IoId poolLevelIoId_ = IO_ID_INVALID;
    IoId waterTempIoId_ = IO_ID_INVALID;
    char poollogicCfgJson_[768]{};
    uint32_t lastLedApplyTryMs_ = 0;
    uint32_t lastLedPageToggleMs_ = 0;
    uint32_t lastWifiBlinkToggleMs_ = 0;

    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void handleDriverEvent_(const HmiEvent& e);
    bool requestRefresh_();
    bool openConfigHome_();
    bool openConfigModule_(const char* module);
    bool setLedPage_(uint8_t page);
    uint8_t getLedPage_() const;
    bool refreshCurrentModule_();
    bool render_();
    bool buildMenuJson_(char* out, size_t outLen) const;
    void refreshPoolLogicFlags_();
    void refreshRuntimeFlags_();
    void refreshAlarmFlags_();
    void refreshWaterLevelFlag_();
    void applyOutputConfig_();
    void applyLedMask_(bool force = false);

    HmiService hmiSvc_{
        ServiceBinding::bind<&HMIModule::requestRefresh_>,
        ServiceBinding::bind<&HMIModule::openConfigHome_>,
        ServiceBinding::bind<&HMIModule::openConfigModule_>,
        ServiceBinding::bind<&HMIModule::buildMenuJson_>,
        ServiceBinding::bind<&HMIModule::setLedPage_>,
        ServiceBinding::bind_or<&HMIModule::getLedPage_, (uint8_t)1U>,
        this
    };
};
