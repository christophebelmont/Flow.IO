#pragma once
/**
 * @file I2CCfgClientModule.h
 * @brief Supervisor-side config service consumer.
 *
 * Terminology note:
 * - App role: "client" (consumes remote cfg service)
 * - I2C role: master (initiates requests toward Flow.IO slave)
 */

#include "Core/ModulePassive.h"
#include "Core/I2cLink.h"
#include "Core/ServiceBinding.h"
#include "Core/I2cCfgProtocol.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class I2CCfgClientModule : public ModulePassive {
public:
    ModuleId moduleId() const override { return ModuleId::I2cCfgClient; }

    uint8_t dependencyCount() const override { return 3; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::Command;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;

private:
    struct ConfigData {
        bool enabled = true;
        int32_t sda = 21;
        int32_t scl = 22;
        int32_t freqHz = 100000;
        uint8_t targetAddr = 0x42;
    } cfgData_{};

    // CFGDOC: {"label":"Client cfg I2C actif", "help":"Active le client de configuration I2C cote Supervisor pour dialoguer avec Flow.IO."}
    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientEnabled), "enabled", "i2c/cfg/client",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO SDA interlink", "help":"GPIO utilise pour la ligne SDA du bus interlink Supervisor -> Flow.IO."}
    ConfigVariable<int32_t, 0> sdaVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientSda), "sda", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.sda, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO SCL interlink", "help":"GPIO utilise pour la ligne SCL du bus interlink Supervisor -> Flow.IO."}
    ConfigVariable<int32_t, 0> sclVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientScl), "scl", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.scl, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Frequence I2C interlink", "help":"Frequence du bus interlink en hertz.", "unit":"Hz"}
    ConfigVariable<int32_t, 0> freqVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientFreq), "freq_hz", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.freqHz, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Adresse cible Flow.IO", "help":"Adresse I2C du serveur de configuration sur Flow.IO (mode esclave)."}
    ConfigVariable<uint8_t, 0> addrVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientAddr), "target_addr", "i2c/cfg/client",
        ConfigType::UInt8, &cfgData_.targetAddr, ConfigPersistence::Persistent, 0
    };

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    I2cLink link_{};
    bool ready_ = false;
    bool reachable_ = false;
    uint8_t seq_ = 1;
    uint32_t retryAfterMs_ = 0;
    MqttConfigRouteProducer cfgMqttPub_{};

    FlowCfgRemoteService svc_{
        ServiceBinding::bind<&I2CCfgClientModule::isReadySvc_>,
        ServiceBinding::bind<&I2CCfgClientModule::listModulesJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::listChildrenJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::getModuleJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::runtimeStatusDomainJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::runtimeStatusJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::applyPatchJson_>,
        this
    };

    void startLink_();
    bool isReadySvc_();
    bool ensureReady_();
    bool isReady_() const;
    bool listModulesJson_(char* out, size_t outLen);
    bool listChildrenJson_(const char* prefix, char* out, size_t outLen);
    bool getModuleJson_(const char* module, char* out, size_t outLen, bool* truncated);
    bool runtimeStatusDomainJson_(FlowStatusDomain domain, char* out, size_t outLen);
    bool runtimeStatusJson_(char* out, size_t outLen);
    bool applyPatchJson_(const char* patch, char* out, size_t outLen);
    bool executeSystemActionJson_(uint8_t action, char* out, size_t outLen);
    bool pingFlow_(uint8_t& statusOut);
    void recoverLinkAfterApplyFailure_(const char* step, bool transportOk, uint8_t status);
    void markRemoteUnavailable_();
    void markRemoteAvailable_();

    bool transact_(uint8_t op,
                   const uint8_t* reqPayload,
                   size_t reqLen,
                   uint8_t& statusOut,
                   uint8_t* respPayload,
                   size_t respPayloadMax,
                   size_t& respLenOut);

    static bool cmdFlowReboot_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen);
    static bool cmdFlowFactoryReset_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen);
};
