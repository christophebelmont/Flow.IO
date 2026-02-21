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
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class I2CCfgServerModule : public ModulePassive {
public:
    const char* moduleId() const override { return "i2ccfg.server"; }

    uint8_t dependencyCount() const override { return 3; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "config";
        if (i == 2) return "datastore";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;

private:
    struct ConfigData {
        bool enabled = true;
        bool useIoBus = false;
        int32_t bus = 1;
        int32_t sda = 12;
        int32_t scl = 14;
        int32_t freqHz = 100000;
        uint8_t address = 0x42;
    } cfgData_{};

    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerEnabled), "enabled", "i2c/cfg/server",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool, 0> useIoBusVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerUseIoBus), "use_io_bus", "i2c/cfg/server",
        ConfigType::Bool, &cfgData_.useIoBus, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> busVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerBus), "bus", "i2c/cfg/server",
        ConfigType::Int32, &cfgData_.bus, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> sdaVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerSda), "sda", "i2c/cfg/server",
        ConfigType::Int32, &cfgData_.sda, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> sclVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerScl), "scl", "i2c/cfg/server",
        ConfigType::Int32, &cfgData_.scl, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> freqVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerFreq), "freq_hz", "i2c/cfg/server",
        ConfigType::Int32, &cfgData_.freqHz, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<uint8_t, 0> addrVar_{
        NVS_KEY(NvsKeys::I2cCfg::ServerAddr), "address", "i2c/cfg/server",
        ConfigType::UInt8, &cfgData_.address, ConfigPersistence::Persistent, 0
    };

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    DataStore* dataStore_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    I2cLink link_{};
    bool started_ = false;

    static constexpr size_t kModuleJsonBufSize = Limits::JsonCfgBuf;
    static constexpr size_t kStatusJsonBufSize = 640;
    static constexpr size_t kPatchBufSize = Limits::JsonCfgBuf + 1U;
    char moduleJson_[kModuleJsonBufSize] = {0};
    size_t moduleJsonLen_ = 0;
    bool moduleJsonValid_ = false;
    bool moduleJsonTruncated_ = false;
    char statusJson_[kStatusJsonBufSize] = {0};
    size_t statusJsonLen_ = 0;
    bool statusJsonValid_ = false;
    bool statusJsonTruncated_ = false;

    char patchBuf_[kPatchBufSize] = {0};
    size_t patchExpected_ = 0;
    size_t patchWritten_ = 0;

    uint32_t reqCount_ = 0;
    uint32_t lastReqMs_ = 0;
    uint32_t badReqCount_ = 0;

    uint8_t txFrame_[I2cCfgProtocol::MaxRespFrame] = {0};
    size_t txFrameLen_ = 0;
    portMUX_TYPE txMux_ = portMUX_INITIALIZER_UNLOCKED;

    static void onReceiveStatic_(void* ctx, const uint8_t* data, size_t len);
    static size_t onRequestStatic_(void* ctx, uint8_t* out, size_t maxLen);

    void onReceive_(const uint8_t* data, size_t len);
    size_t onRequest_(uint8_t* out, size_t maxLen);

    void startLink_();
    void resetPatchState_();
    bool buildRuntimeStatusJson_(bool& truncatedOut);
    bool resolveIoPins_(int32_t& sdaOut, int32_t& sclOut) const;
    void buildResponse_(uint8_t op, uint8_t seq, uint8_t status, const uint8_t* payload, size_t payloadLen);
    void handleRequest_(uint8_t op, uint8_t seq, const uint8_t* payload, size_t payloadLen);
};
