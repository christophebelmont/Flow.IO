#pragma once
/**
 * @file I2CCfgServerModule.h
 * @brief Flow.IO-side config service endpoint.
 *
 * Terminology note:
 * - App role: "server" (exposes remote cfg service)
 * - I2C role: slave (answers requests initiated by Supervisor)
 */

#include "Core/ModulePassive.h"
#include "Core/I2cLink.h"
#include "Core/I2cCfgProtocol.h"
#include "Core/RuntimeUi.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class I2CCfgServerModule : public ModulePassive, public IRuntimeUiValueProvider {
public:
    ModuleId moduleId() const override { return ModuleId::I2cCfgServer; }
    ModuleId runtimeUiProviderModuleId() const override { return moduleId(); }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::DataStore;
        if (i == 3) return ModuleId::Command;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;
    void onStart(ConfigStore&, ServiceRegistry&) override;
    uint8_t taskCount() const override { return started_ ? 1 : 0; }
    const ModuleTaskSpec* taskSpecs() const override;
    bool registerRuntimeUiProvider(const IRuntimeUiValueProvider* provider);
    bool writeRuntimeUiValue(uint8_t valueId, IRuntimeUiWriter& writer) const override;

private:
    struct ConfigData {
        bool enabled = true;
        int32_t sda = 5;
        int32_t scl = 15;
        int32_t freqHz = 100000;
        uint8_t address = 0x42;
    } cfgData_{};

    // CFGDOC: {"label":"Serveur eLink actif", "help":"Active le serveur eLink cote Flow.io pour repondre au Supervisor."}
    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerEnabled), "enabled", "elink/server",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO SDA interlink", "help":"GPIO utilise pour la ligne SDA du bus interlink Flow.io <-> Supervisor."}
    ConfigVariable<int32_t, 0> sdaVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerSda), "sda", "elink/server",
        ConfigType::Int32, &cfgData_.sda, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"GPIO SCL interlink", "help":"GPIO utilise pour la ligne SCL du bus interlink Flow.io <-> Supervisor."}
    ConfigVariable<int32_t, 0> sclVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerScl), "scl", "elink/server",
        ConfigType::Int32, &cfgData_.scl, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Frequence eLink", "help":"Frequence du bus eLink en hertz.", "unit":"Hz"}
    ConfigVariable<int32_t, 0> freqVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerFreq), "freq_hz", "elink/server",
        ConfigType::Int32, &cfgData_.freqHz, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Adresse locale Flow.io", "help":"Adresse I2C locale du serveur de configuration Flow.io (mode esclave)."}
    ConfigVariable<uint8_t, 0> addrVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerAddr), "address", "elink/server",
        ConfigType::UInt8, &cfgData_.address, ConfigPersistence::Persistent, 0
    };

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    I2cLink link_{};
    bool started_ = false;

    enum class PendingSystemAction : uint8_t {
        None = 0,
        Reboot = 1,
        FactoryReset = 2
    };
    volatile uint8_t pendingAction_ = 0;
    portMUX_TYPE actionMux_ = portMUX_INITIALIZER_UNLOCKED;

    static constexpr size_t kModuleJsonBufSize = Limits::JsonCfgBuf;
    static constexpr size_t kStatusJsonBufSize = 448;
    static constexpr size_t kPatchBufSize = Limits::JsonCfgBuf + 1U;
    static constexpr size_t kPoolModeJsonBufSize = 192;
    static constexpr size_t kAlarmSnapshotBufSize = Limits::Alarm::SnapshotJsonBuf;
    static constexpr uint8_t kMaxAlarmCodes = 10;
    char moduleJson_[kModuleJsonBufSize] = {0};
    size_t moduleJsonLen_ = 0;
    bool moduleJsonValid_ = false;
    bool moduleJsonTruncated_ = false;
    char statusJson_[kStatusJsonBufSize] = {0};
    size_t statusJsonLen_ = 0;
    bool statusJsonValid_ = false;
    bool statusJsonTruncated_ = false;
    char poolModeJsonScratch_[kPoolModeJsonBufSize] = {0};
    char alarmJsonScratch_[kAlarmSnapshotBufSize] = {0};
    char activeAlarmCodes_[kMaxAlarmCodes][24] = {{0}};
    size_t alarmSnapshotLen_ = 0;
    bool alarmSnapshotValid_ = false;
    bool alarmSnapshotTruncated_ = false;

    char patchBuf_[kPatchBufSize] = {0};
    size_t patchExpected_ = 0;
    size_t patchWritten_ = 0;

    uint32_t reqCount_ = 0;
    uint32_t lastReqMs_ = 0;
    uint32_t badReqCount_ = 0;

    uint8_t txFrame_[I2cCfgProtocol::MaxRespFrame] = {0};
    size_t txFrameLen_ = 0;
    portMUX_TYPE txMux_ = portMUX_INITIALIZER_UNLOCKED;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
    RuntimeUiRegistry runtimeUiRegistry_{};
    RuntimeUiService runtimeUiSvc_{runtimeUiRegistry_};

    static void onReceiveStatic_(void* ctx, const uint8_t* data, size_t len);
    static size_t onRequestStatic_(void* ctx, uint8_t* out, size_t maxLen);

    void onReceive_(const uint8_t* data, size_t len);
    size_t onRequest_(uint8_t* out, size_t maxLen);

    void startLink_();
    void resetPatchState_();
    static bool isValidStatusDomain_(FlowStatusDomain domain);
    bool collectPoolModeFlags_(bool& hasModeOut,
                               bool& autoModeOut,
                               bool& winterModeOut,
                               bool& phAutoModeOut,
                               bool& orpAutoModeOut);
    void collectActiveAlarmCodes_(uint8_t& activeAlarmCountOut, uint8_t& activeAlarmCodeCountOut);
    bool buildRuntimeStatusDomainJson_(FlowStatusDomain domain, bool& truncatedOut);
    bool buildRuntimeStatusSystemJson_(bool& truncatedOut);
    bool buildRuntimeStatusWifiJson_(bool& truncatedOut);
    bool buildRuntimeStatusMqttJson_(bool& truncatedOut);
    bool buildRuntimeStatusI2cJson_(bool& truncatedOut);
    bool buildRuntimeStatusPoolJson_(bool& truncatedOut);
    bool buildRuntimeStatusAlarmJson_(bool& truncatedOut);
    bool buildRuntimeAlarmSnapshotJson_(bool& truncatedOut);
    void queueSystemAction_(PendingSystemAction action);
    PendingSystemAction takePendingSystemAction_();
    void actionLoop_();
    static void actionTaskStatic_(void* ctx);
    void buildResponse_(uint8_t op, uint8_t seq, uint8_t status, const uint8_t* payload, size_t payloadLen);
    void handleRequest_(uint8_t op, uint8_t seq, const uint8_t* payload, size_t payloadLen);
};
