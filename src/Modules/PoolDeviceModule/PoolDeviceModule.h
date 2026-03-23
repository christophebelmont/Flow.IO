#pragma once
/**
 * @file PoolDeviceModule.h
 * @brief Pool actuator domain layer above IOModule.
 *
 * Public facade only. The implementation is split across Lifecycle / Control /
 * Runtime / Commands translation units.
 */

#include "Core/Module.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/CommandRegistry.h"
#include "Core/ConfigTypes.h"
#include "Modules/PoolDeviceModule/PoolDeviceModuleDataModel.h"

enum PoolDeviceType : uint8_t {
    POOL_DEVICE_FILTRATION = 0,
    POOL_DEVICE_PERISTALTIC = 1,
    POOL_DEVICE_RELAY_STD = 2
};

struct PoolDeviceDefinition {
    char label[24] = {0};
    /** Required IOServiceV2 digital output id bound to this pool device. */
    IoId ioId = IO_ID_INVALID;
    uint8_t type = POOL_DEVICE_RELAY_STD;
    bool enabled = true;
    float flowLPerHour = 0.0f;     // used for dosing volumes
    float tankCapacityMl = 0.0f;   // 0 means "not tracked"
    float tankInitialMl = 0.0f;    // <=0 means "use capacity"
    uint8_t dependsOnMask = 0;     // bit per pool-device slot
    int32_t maxUptimeDaySec = 0;   // 0 means "unlimited"
};

class PoolDeviceModule : public Module, public IRuntimeSnapshotProvider {
public:
    ModuleId moduleId() const override { return ModuleId::PoolDevice; }
    const char* taskName() const override { return "pooldev"; }
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 8; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::DataStore;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::Time;
        if (i == 4) return ModuleId::Io;
        if (i == 5) return ModuleId::Mqtt;
        if (i == 6) return ModuleId::EventBus;
        if (i == 7) return ModuleId::Ha;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

    bool defineDevice(const PoolDeviceDefinition& def);
    const char* deviceLabel(uint8_t idx) const;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    RuntimeRouteClass runtimeSnapshotClass(uint8_t idx) const override;
    bool runtimeSnapshotAffectsKey(uint8_t idx, DataKey key) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;

private:
    static constexpr uint8_t RESET_PENDING_DAY = (1u << 0);
    static constexpr uint8_t RESET_PENDING_WEEK = (1u << 1);
    static constexpr uint8_t RESET_PENDING_MONTH = (1u << 2);
    static constexpr uint32_t RUNTIME_PERSIST_INTERVAL_MS = 60000U;
    static constexpr uint32_t MIN_VALID_EPOCH_SEC = 1609459200U; // 2021-01-01

    struct PoolDeviceSlot {
        bool used = false;
        char id[8] = {0};          // stable runtime id: pdN
        PoolDeviceDefinition def{};
        /** Cached hardware endpoint id (copied from definition at registration). */
        IoId ioId = IO_ID_INVALID;

        bool desiredOn = false;
        bool actualOn = false;
        uint8_t blockReason = POOL_DEVICE_BLOCK_NONE;

        uint32_t lastTickMs = 0;
        uint64_t runningMsDay = 0;
        uint64_t runningMsWeek = 0;
        uint64_t runningMsMonth = 0;
        uint64_t runningMsTotal = 0;
        float injectedMlDay = 0.0f;
        float injectedMlWeek = 0.0f;
        float injectedMlMonth = 0.0f;
        float injectedMlTotal = 0.0f;
        float tankRemainingMl = 0.0f;
        uint32_t dayKey = 0;
        uint32_t weekKey = 0;
        uint32_t monthKey = 0;
        uint32_t stateTsMs = 0;
        uint32_t metricsTsMs = 0;
        uint32_t lastRuntimeCommitMs = 0;
        uint32_t lastPersistMs = 0;
        bool hasPersistedMetrics = false;
        bool persistDirty = false;
        bool persistImmediate = false;
        bool forceMetricsCommit = false;
    };

    struct PeriodKeys {
        uint32_t day = 0;
        uint32_t week = 0;
        uint32_t month = 0;
    };

    // Lifecycle
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);

    // Control
    uint8_t activeCount_() const;
    PoolDeviceSvcStatus svcMetaImpl_(uint8_t slot, PoolDeviceSvcMeta* outMeta) const;
    PoolDeviceSvcStatus svcReadActualOnImpl_(uint8_t slot, uint8_t* outOn, uint32_t* outTsMs) const;
    PoolDeviceSvcStatus svcWriteDesiredImpl_(uint8_t slot, uint8_t on);
    PoolDeviceSvcStatus svcRefillTankImpl_(uint8_t slot, float remainingMl);
    void tickDevices_(uint32_t nowMs);
    void resetDailyCounters_();
    void resetWeeklyCounters_();
    void resetMonthlyCounters_();
    void requestPeriodReconcile_();
    bool weekStartMondayFromConfig_() const;
    bool currentPeriodKeys_(PeriodKeys& out) const;
    bool reconcilePeriodCountersFromClock_();
    bool loadPersistedMetrics_(uint8_t slotIdx, PoolDeviceSlot& slot);
    bool persistMetrics_(uint8_t slotIdx, PoolDeviceSlot& slot, uint32_t nowMs);
    bool dependenciesSatisfied_(uint8_t slotIdx) const;
    static bool maxUptimeReached_(const PoolDeviceSlot& slot);
    bool readIoState_(const PoolDeviceSlot& slot, bool& onOut) const;
    bool writeIo_(IoId ioId, bool on);
    static uint32_t toSeconds_(uint64_t ms);

    // Runtime
    bool configureRuntime_();
    static MqttBuildResult buildCfgBasePdmStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static MqttBuildResult buildCfgBasePdmrtStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    MqttBuildResult buildCfgBasePdm_(MqttBuildContext& buildCtx);
    MqttBuildResult buildCfgBasePdmrt_(MqttBuildContext& buildCtx);
    bool snapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& slotIdxOut, bool& metricsOut) const;
    bool buildStateSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildMetricsSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const;
    static const char* typeStr_(uint8_t type);
    static const char* blockReasonStr_(uint8_t reason);

    // Commands
    static bool cmdPoolWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdPoolRefill_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    bool handlePoolWrite_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handlePoolRefill_(const CommandRequest& req, char* reply, size_t replyLen);

    // State and configuration storage
    bool runtimeReady_ = false;
    portMUX_TYPE resetMux_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t resetPendingMask_ = 0;
    bool periodReconcilePending_ = true;

    char cfgModuleName_[POOL_DEVICE_MAX][16]{};
    char cfgRuntimeModuleName_[POOL_DEVICE_MAX][16]{};
    char nvsEnabledKey_[POOL_DEVICE_MAX][16]{};
    char nvsTypeKey_[POOL_DEVICE_MAX][16]{};
    char nvsDependsKey_[POOL_DEVICE_MAX][16]{};
    char nvsFlowKey_[POOL_DEVICE_MAX][16]{};
    char nvsTankCapKey_[POOL_DEVICE_MAX][16]{};
    char nvsTankInitKey_[POOL_DEVICE_MAX][16]{};
    char nvsMaxUptimeKey_[POOL_DEVICE_MAX][16]{};
    char nvsRuntimeKey_[POOL_DEVICE_MAX][16]{};
    char runtimePersistBuf_[POOL_DEVICE_MAX][192]{};
    PoolDeviceSlot slots_[POOL_DEVICE_MAX]{};

    // Services and shared runtime integrations
    const LogHubService* logHub_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;
    const HAService* haSvc_ = nullptr;
    PoolDeviceService poolSvc_{
        ServiceBinding::bind<&PoolDeviceModule::activeCount_>,
        ServiceBinding::bind<&PoolDeviceModule::svcMetaImpl_>,
        ServiceBinding::bind<&PoolDeviceModule::svcReadActualOnImpl_>,
        ServiceBinding::bind<&PoolDeviceModule::svcWriteDesiredImpl_>,
        ServiceBinding::bind<&PoolDeviceModule::svcRefillTankImpl_>,
        this
    };
    EventBus* eventBus_ = nullptr;
    DataStore* dataStore_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
    MqttConfigRouteProducer::Route* cfgRoutes_ = nullptr;
    uint8_t cfgRouteCount_ = 0;

    // CFGDOC: {"label":"Activation appareil","help":"Active ou désactive l'appareil du pool concerne."}
    ConfigVariable<bool,0> cfgEnabledVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Type appareil","help":"Type logique de l'appareil (0=filtration, 1=peristaltique, 2=relais standard)."}
    ConfigVariable<uint8_t,0> cfgTypeVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Masque dépendances","help":"Masque de dépendances inter-appareils."}
    ConfigVariable<uint8_t,0> cfgDependsVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Débit (L/h)","help":"Débit nominal en litres par heure pour le calcul d'injection.","unit":"L/h"}
    ConfigVariable<float,0> cfgFlowVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Capacité cuve (mL)","help":"Capacité maximale de la cuve associée à l'appareil.","unit":"mL"}
    ConfigVariable<float,0> cfgTankCapVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Niveau initial cuve (mL)","help":"Volume initial de cuve après configuration/reset.","unit":"mL"}
    ConfigVariable<float,0> cfgTankInitVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Uptime max journalier (s)","help":"Temps maximal autorisé par jour pour l'appareil.","unit":"s"}
    ConfigVariable<int32_t,0> cfgMaxUptimeVar_[POOL_DEVICE_MAX]{};
    // CFGDOC: {"label":"Blob métriques","help":"État/métriques persistees de l'appareil pour reprise."}
    ConfigVariable<char,0> cfgRuntimeVar_[POOL_DEVICE_MAX]{};
};
