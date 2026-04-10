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
#include "Core/RuntimeUi.h"
#include "Core/SystemLimits.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include "Modules/Network/I2CCfgClientModule/I2CCfgClientModuleDataModel.h"
#include <freertos/semphr.h>

class I2CCfgClientModule : public ModulePassive {
public:
    ModuleId moduleId() const override { return ModuleId::I2cCfgClient; }
    const char* taskName() const override { return "i2c_cfg_client"; }
    uint16_t taskStackSize() const override { return 5120; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::DataStore;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void onStart(ConfigStore&, ServiceRegistry&) override;
    void loop() override;

private:
    static constexpr uint8_t kDashboardSlotCount = kFlowRemoteDashboardSlotCount;
    static constexpr size_t kRuntimeStatusDomainCacheCount = (size_t)FlowStatusDomain::Pool;
    static constexpr size_t kRuntimeUiMirrorMaxEntries = 64U;
    struct RuntimeUiCacheEntry {
        RuntimeUiId id = 0U;
        uint32_t fetchedAtMs = 0U;
        uint8_t len = 0U;
        bool valid = false;
        uint8_t data[I2cCfgProtocol::MaxPayload] = {0};
    };
    struct ConfigData {
        bool enabled = true;
        int32_t sda = 27;
        int32_t scl = 13;
        int32_t freqHz = 100000;
        uint8_t targetAddr = 0x42;
    } cfgData_{};
    struct DashboardSlotConfig {
        bool enabled = true;
        uint16_t runtimeUiId = 0U;
        char label[24]{};
        uint8_t colorId = 0U;
    };

    // CFGDOC: {"label":"Client eLink actif", "help":"Active le client eLink cote Supervisor pour dialoguer avec Flow.io."}
    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientEnabled), "enabled", "elink/client",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO SDA interlink", "help":"GPIO utilise pour la ligne SDA du bus interlink Supervisor -> Flow.io."}
    ConfigVariable<int32_t, 0> sdaVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientSda), "sda", "elink/client",
        ConfigType::Int32, &cfgData_.sda, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO SCL interlink", "help":"GPIO utilise pour la ligne SCL du bus interlink Supervisor -> Flow.io."}
    ConfigVariable<int32_t, 0> sclVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientScl), "scl", "elink/client",
        ConfigType::Int32, &cfgData_.scl, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Frequence eLink", "help":"Frequence du bus eLink en hertz.", "unit":"Hz"}
    ConfigVariable<int32_t, 0> freqVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientFreq), "freq_hz", "elink/client",
        ConfigType::Int32, &cfgData_.freqHz, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Adresse cible Flow.io", "help":"Adresse I2C du serveur de configuration sur Flow.io (mode esclave)."}
    ConfigVariable<uint8_t, 0> addrVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientAddr), "target_addr", "elink/client",
        ConfigType::UInt8, &cfgData_.targetAddr, ConfigPersistence::Persistent, 0
    };
    DashboardSlotConfig dashboardCfg_[kDashboardSlotCount]{};
    ConfigVariable<bool, 0> dashboardEnabledVars_[kDashboardSlotCount]{};
    ConfigVariable<uint16_t, 0> dashboardRuntimeIdVars_[kDashboardSlotCount]{};
    ConfigVariable<char, 0> dashboardLabelVars_[kDashboardSlotCount]{};
    ConfigVariable<uint8_t, 0> dashboardColorIdVars_[kDashboardSlotCount]{};
    char dashboardModuleNames_[kDashboardSlotCount][28]{};
    char dashboardEnabledKeys_[kDashboardSlotCount][16]{};
    char dashboardRuntimeIdKeys_[kDashboardSlotCount][16]{};
    char dashboardLabelKeys_[kDashboardSlotCount][16]{};
    char dashboardColorIdKeys_[kDashboardSlotCount][16]{};

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    I2cLink link_{};
    bool ready_ = false;
    bool reachable_ = false;
    uint8_t seq_ = 1;
    uint32_t retryAfterMs_ = 0;
    MqttConfigRouteProducer cfgMqttPub_{};
    SemaphoreHandle_t runtimeCacheMutex_ = nullptr;
    SemaphoreHandle_t requestMutex_ = nullptr;
    SemaphoreHandle_t transportMutex_ = nullptr;
    bool runtimeCacheValid_ = false;
    uint32_t runtimeCacheFetchedAtMs_ = 0;
    uint32_t nextRuntimeCacheRefreshAtMs_ = 0;
    uint32_t priorityI2cBusyUntilMs_ = 0;
    char runtimeStatusDomainCache_[kRuntimeStatusDomainCacheCount][640] = {{0}};
    bool runtimeStatusDomainCacheValid_[kRuntimeStatusDomainCacheCount] = {false};
    RuntimeUiCacheEntry runtimeUiCache_[kRuntimeUiMirrorMaxEntries] = {};
    char runtimeStatusDomainFetchScratch_[640] = {0};
    char runtimeStatusDomainCacheNext_[kRuntimeStatusDomainCacheCount][640] = {{0}};
    bool runtimeStatusDomainCacheValidNext_[kRuntimeStatusDomainCacheCount] = {false};

    FlowCfgRemoteService svc_{
        ServiceBinding::bind<&I2CCfgClientModule::isReadySvc_>,
        ServiceBinding::bind<&I2CCfgClientModule::listModulesJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::listChildrenJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::getModuleJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::runtimeStatusDomainJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::runtimeStatusJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::runtimeAlarmSnapshotJson_>,
        ServiceBinding::bind<&I2CCfgClientModule::runtimeUiValues_>,
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
    bool runtimeAlarmSnapshotJson_(char* out, size_t outLen);
    bool runtimeUiValues_(const RuntimeUiId* ids, uint8_t count, uint8_t* out, size_t outLen, size_t* writtenOut);
    bool applyPatchJson_(const char* patch, char* out, size_t outLen);
    bool executeSystemActionJson_(uint8_t action, char* out, size_t outLen);
    bool pingFlow_(uint8_t& statusOut);
    void recoverLinkAfterApplyFailure_(const char* step, bool transportOk, uint8_t status);
    void markRemoteUnavailable_();
    void markRemoteAvailable_();
    bool beginRequestSession_(TickType_t timeoutTicks, bool interactive);
    void endRequestSession_();
    void notePriorityI2cRequest_(uint32_t holdMs);
    bool priorityI2cWindowActive_() const;
    void invalidateRuntimeCache_();
    bool refreshRuntimeCacheIfNeeded_(bool force = false);
    void publishFlowRemoteReady_(bool ready);
    bool parseFlowRemoteSnapshotFromCache_(FlowRemoteRuntimeData& out);
    void publishFlowRemoteSnapshotFromCache_();
    uint8_t buildRuntimeMirrorIdList_(RuntimeUiId* idsOut, uint8_t maxCount) const;
    bool fetchRuntimeStatusDomainUncached_(FlowStatusDomain domain, char* out, size_t outLen);
    bool fetchRuntimeUiValuesUncached_(const RuntimeUiId* ids, uint8_t count, uint8_t* out, size_t outLen, size_t* writtenOut);
    bool composeRuntimeUiValuesFromCache_(const RuntimeUiId* ids, uint8_t count, uint8_t* out, size_t outLen, bool allowStale, bool* allFreshOut);
    bool cacheRuntimeUiPayload_(const uint8_t* payload, size_t payloadLen);
    RuntimeUiCacheEntry* findRuntimeUiCacheEntry_(RuntimeUiId id);
    const RuntimeUiCacheEntry* findRuntimeUiCacheEntry_(RuntimeUiId id) const;
    RuntimeUiCacheEntry* allocateRuntimeUiCacheEntry_(RuntimeUiId id);
    bool parseRuntimeUiRecord_(const uint8_t* payload, size_t payloadLen, size_t offset, RuntimeUiId* idOut, size_t* recordLenOut) const;
    static uint8_t runtimeStatusDomainCacheIndex_(FlowStatusDomain domain);

    bool transact_(uint8_t op,
                   const uint8_t* reqPayload,
                   size_t reqLen,
                   uint8_t& statusOut,
                   uint8_t* respPayload,
                   size_t respPayloadMax,
                   size_t& respLenOut);
    bool transactUnlocked_(uint8_t op,
                           const uint8_t* reqPayload,
                           size_t reqLen,
                           uint8_t& statusOut,
                           uint8_t* respPayload,
                           size_t respPayloadMax,
                           size_t& respLenOut);

    static bool cmdFlowReboot_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen);
    static bool cmdFlowFactoryReset_(void* userCtx, const CommandRequest&, char* reply, size_t replyLen);
};
