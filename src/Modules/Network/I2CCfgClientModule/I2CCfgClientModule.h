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
#include "Core/I2cCfgProtocol.h"
#include "Core/ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"

class I2CCfgClientModule : public ModulePassive {
public:
    const char* moduleId() const override { return "i2ccfg.client"; }

    uint8_t dependencyCount() const override { return 2; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "config";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry&) override;

private:
    struct ConfigData {
        bool enabled = true;
        bool useIoBus = false;
        int32_t bus = 0;
        int32_t sda = 21;
        int32_t scl = 22;
        int32_t freqHz = 100000;
        uint8_t targetAddr = 0x42;
    } cfgData_{};

    ConfigVariable<bool, 0> enabledVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientEnabled), "enabled", "i2c/cfg/client",
        ConfigType::Bool, &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<bool, 0> useIoBusVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientUseIoBus), "use_io_bus", "i2c/cfg/client",
        ConfigType::Bool, &cfgData_.useIoBus, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> busVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientBus), "bus", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.bus, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> sdaVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientSda), "sda", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.sda, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> sclVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientScl), "scl", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.scl, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t, 0> freqVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientFreq), "freq_hz", "i2c/cfg/client",
        ConfigType::Int32, &cfgData_.freqHz, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<uint8_t, 0> addrVar_{
        NVS_KEY(NvsKeys::I2cCfg::ClientAddr), "target_addr", "i2c/cfg/client",
        ConfigType::UInt8, &cfgData_.targetAddr, ConfigPersistence::Persistent, 0
    };

    const LogHubService* logHub_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    I2cLink link_{};
    bool ready_ = false;
    uint8_t seq_ = 1;

    FlowCfgRemoteService svc_{
        svcIsReady_,
        svcListModulesJson_,
        svcListChildrenJson_,
        svcGetModuleJson_,
        svcRuntimeStatusJson_,
        svcApplyPatchJson_,
        this
    };

    void startLink_();
    bool ensureReady_();
    bool isReady_() const;
    bool listModulesJson_(char* out, size_t outLen);
    bool listChildrenJson_(const char* prefix, char* out, size_t outLen);
    bool getModuleJson_(const char* module, char* out, size_t outLen, bool* truncated);
    bool runtimeStatusJson_(char* out, size_t outLen);
    bool applyPatchJson_(const char* patch, char* out, size_t outLen);
    bool resolveIoPins_(int32_t& sdaOut, int32_t& sclOut) const;
    bool pingFlow_(uint8_t& statusOut);

    bool transact_(uint8_t op,
                   const uint8_t* reqPayload,
                   size_t reqLen,
                   uint8_t& statusOut,
                   uint8_t* respPayload,
                   size_t respPayloadMax,
                   size_t& respLenOut);

    static bool svcIsReady_(void* ctx);
    static bool svcListModulesJson_(void* ctx, char* out, size_t outLen);
    static bool svcListChildrenJson_(void* ctx, const char* prefix, char* out, size_t outLen);
    static bool svcGetModuleJson_(void* ctx, const char* module, char* out, size_t outLen, bool* truncated);
    static bool svcRuntimeStatusJson_(void* ctx, char* out, size_t outLen);
    static bool svcApplyPatchJson_(void* ctx, const char* patch, char* out, size_t outLen);
};
