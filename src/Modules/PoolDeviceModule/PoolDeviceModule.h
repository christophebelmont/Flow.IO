#pragma once
/**
 * @file PoolDeviceModule.h
 * @brief Pool actuator domain layer above IOModule.
 */

#include "Core/Module.h"
#include "Core/RuntimeSnapshotProvider.h"
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
};

class PoolDeviceModule : public Module, public IRuntimeSnapshotProvider {
public:
    const char* moduleId() const override { return "pooldev"; }
    const char* taskName() const override { return "pooldev"; }

    uint8_t dependencyCount() const override { return 8; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "datastore";
        if (i == 2) return "cmd";
        if (i == 3) return "time";
        if (i == 4) return "io";
        if (i == 5) return "mqtt";
        if (i == 6) return "eventbus";
        if (i == 7) return "ha";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

    bool defineDevice(const PoolDeviceDefinition& def);
    const char* deviceLabel(uint8_t idx) const;
    uint8_t runtimeSnapshotCount() const override;
    const char* runtimeSnapshotSuffix(uint8_t idx) const override;
    bool buildRuntimeSnapshot(uint8_t idx, char* out, size_t len, uint32_t& maxTsOut) const override;

private:
    static constexpr uint8_t RESET_PENDING_DAY = (1u << 0);
    static constexpr uint8_t RESET_PENDING_WEEK = (1u << 1);
    static constexpr uint8_t RESET_PENDING_MONTH = (1u << 2);

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
        uint32_t stateTsMs = 0;
        uint32_t metricsTsMs = 0;
        uint32_t lastRuntimeCommitMs = 0;
        bool forceMetricsCommit = false;
    };

    static bool cmdPoolWrite_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdPoolRefill_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    bool handlePoolWrite_(const CommandRequest& req, char* reply, size_t replyLen);
    bool handlePoolRefill_(const CommandRequest& req, char* reply, size_t replyLen);

    static uint8_t svcCount_(void* ctx);
    static PoolDeviceSvcStatus svcMeta_(void* ctx, uint8_t slot, PoolDeviceSvcMeta* outMeta);
    static PoolDeviceSvcStatus svcReadActualOn_(void* ctx, uint8_t slot, uint8_t* outOn, uint32_t* outTsMs);
    static PoolDeviceSvcStatus svcWriteDesired_(void* ctx, uint8_t slot, uint8_t on);
    static PoolDeviceSvcStatus svcRefillTank_(void* ctx, uint8_t slot, float remainingMl);

    uint8_t activeCount_() const;
    PoolDeviceSvcStatus svcMetaImpl_(uint8_t slot, PoolDeviceSvcMeta* outMeta) const;
    PoolDeviceSvcStatus svcReadActualOnImpl_(uint8_t slot, uint8_t* outOn, uint32_t* outTsMs) const;
    PoolDeviceSvcStatus svcWriteDesiredImpl_(uint8_t slot, uint8_t on);
    PoolDeviceSvcStatus svcRefillTankImpl_(uint8_t slot, float remainingMl);

    bool configureRuntime_();
    void tickDevices_(uint32_t nowMs);
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);
    void resetDailyCounters_();
    void resetWeeklyCounters_();
    void resetMonthlyCounters_();
    bool snapshotRouteFromIndex_(uint8_t snapshotIdx, uint8_t& slotIdxOut, bool& metricsOut) const;
    bool buildStateSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const;
    bool buildMetricsSnapshot_(uint8_t slotIdx, char* out, size_t len, uint32_t& maxTsOut) const;
    bool dependenciesSatisfied_(uint8_t slotIdx) const;
    bool readIoState_(const PoolDeviceSlot& slot, bool& onOut) const;
    bool writeIo_(IoId ioId, bool on);
    static uint32_t toSeconds_(uint64_t ms);
    static const char* typeStr_(uint8_t type);
    static const char* blockReasonStr_(uint8_t reason);

    const LogHubService* logHub_ = nullptr;
    const IOServiceV2* ioSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const HAService* haSvc_ = nullptr;
    PoolDeviceService poolSvc_{
        svcCount_,
        svcMeta_,
        svcReadActualOn_,
        svcWriteDesired_,
        svcRefillTank_,
        this
    };
    EventBus* eventBus_ = nullptr;
    DataStore* dataStore_ = nullptr;
    bool runtimeReady_ = false;
    portMUX_TYPE resetMux_ = portMUX_INITIALIZER_UNLOCKED;
    uint8_t resetPendingMask_ = 0;

    char cfgModuleName_[POOL_DEVICE_MAX][16]{};
    char nvsEnabledKey_[POOL_DEVICE_MAX][16]{};
    char nvsTypeKey_[POOL_DEVICE_MAX][16]{};
    char nvsDependsKey_[POOL_DEVICE_MAX][16]{};
    char nvsFlowKey_[POOL_DEVICE_MAX][16]{};
    char nvsTankCapKey_[POOL_DEVICE_MAX][16]{};
    char nvsTankInitKey_[POOL_DEVICE_MAX][16]{};
    ConfigVariable<bool,0> cfgEnabledVar_[POOL_DEVICE_MAX]{};
    ConfigVariable<uint8_t,0> cfgTypeVar_[POOL_DEVICE_MAX]{};
    ConfigVariable<uint8_t,0> cfgDependsVar_[POOL_DEVICE_MAX]{};
    ConfigVariable<float,0> cfgFlowVar_[POOL_DEVICE_MAX]{};
    ConfigVariable<float,0> cfgTankCapVar_[POOL_DEVICE_MAX]{};
    ConfigVariable<float,0> cfgTankInitVar_[POOL_DEVICE_MAX]{};

    PoolDeviceSlot slots_[POOL_DEVICE_MAX]{};
};
