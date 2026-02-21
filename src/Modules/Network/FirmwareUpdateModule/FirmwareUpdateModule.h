#pragma once
/**
 * @file FirmwareUpdateModule.h
 * @brief Supervisor firmware updater (Flow.IO + Nextion).
 */

#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Core/ConfigTypes.h"
#include "Core/CommandRegistry.h"

class FirmwareUpdateModule : public Module {
public:
    const char* moduleId() const override { return "fwupdate"; }
    const char* taskName() const override { return "fwupdate"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 8192; }

    uint8_t dependencyCount() const override { return 4; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "wifi";
        if (i == 2) return "cmd";
        if (i == 3) return "webinterface";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;

private:
    static constexpr size_t kUrlLen = 192;
    static constexpr size_t kMsgLen = 120;

    enum class UpdateState : uint8_t {
        Idle = 0,
        Queued,
        Downloading,
        Flashing,
        Done,
        Error
    };

    struct UpdateJob {
        bool pending = false;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::FlowIO;
        char url[kUrlLen] = {0};
    };

    struct UpdateStatus {
        UpdateState state = UpdateState::Idle;
        FirmwareUpdateTarget target = FirmwareUpdateTarget::FlowIO;
        uint8_t progress = 0;
        uint32_t updatedAtMs = 0;
        char msg[kMsgLen] = {0};
    };

    struct ConfigData {
        char updateHost[64] = "";
        char flowioPath[64] = "/build/FlowIO/firmware.bin";
        char supervisorPath[64] = "/build/Supervisor/firmware.bin";
        char nextionPath[64] = "/build/Nextion.tft";
    } cfgData_{};

    ConfigVariable<char, 2> updateHostVar_{
        NVS_KEY("up_host"), "update_host", "fwupdate",
        ConfigType::CharArray, cfgData_.updateHost, ConfigPersistence::Persistent, sizeof(cfgData_.updateHost)
    };
    ConfigVariable<char, 2> flowioPathVar_{
        NVS_KEY("up_flow_path"), "flowio_path", "fwupdate",
        ConfigType::CharArray, cfgData_.flowioPath, ConfigPersistence::Persistent, sizeof(cfgData_.flowioPath)
    };
    ConfigVariable<char, 2> supervisorPathVar_{
        NVS_KEY("up_sup_path"), "supervisor_path", "fwupdate",
        ConfigType::CharArray, cfgData_.supervisorPath, ConfigPersistence::Persistent, sizeof(cfgData_.supervisorPath)
    };
    ConfigVariable<char, 2> nextionPathVar_{
        NVS_KEY("up_nx_path"), "nextion_path", "fwupdate",
        ConfigType::CharArray, cfgData_.nextionPath, ConfigPersistence::Persistent, sizeof(cfgData_.nextionPath)
    };

    ServiceRegistry* services_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const WebInterfaceService* webInterfaceSvc_ = nullptr;

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    UpdateJob queuedJob_{};
    UpdateStatus status_{};
    bool busy_ = false;
    uint32_t activeTotalBytes_ = 0;
    uint32_t activeSentBytes_ = 0;

    static bool svcStart_(void* ctx,
                          FirmwareUpdateTarget target,
                          const char* url,
                          char* errOut,
                          size_t errOutLen);
    static bool svcStatusJson_(void* ctx, char* out, size_t outLen);
    static bool svcConfigJson_(void* ctx, char* out, size_t outLen);
    static bool svcSetConfig_(void* ctx,
                              const char* updateHost,
                              const char* flowioPath,
                              const char* supervisorPath,
                              const char* nextionPath,
                              char* errOut,
                              size_t errOutLen);

    static bool cmdStatus_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFlowIo_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSupervisor_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool startUpdate_(FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool statusJson_(char* out, size_t outLen);
    bool configJson_(char* out, size_t outLen) const;
    bool setConfig_(const char* updateHost,
                    const char* flowioPath,
                    const char* supervisorPath,
                    const char* nextionPath,
                    char* errOut,
                    size_t errOutLen);
    bool runJob_(const UpdateJob& job);
    bool runFlowIoUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runSupervisorUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool resolveUrl_(FirmwareUpdateTarget target,
                     const char* explicitUrl,
                     char* out,
                     size_t outLen,
                     char* errOut,
                     size_t errOutLen) const;
    bool parseUrlArg_(const CommandRequest& req, char* out, size_t outLen) const;
    void setStatus_(UpdateState state, FirmwareUpdateTarget target, uint8_t progress, const char* msg);
    void setError_(FirmwareUpdateTarget target, const char* msg);
    void onProgressChunk_(uint32_t chunkBytes);
    void attachWebInterfaceSvcIfNeeded_();
    static const char* stateStr_(UpdateState s);
    static const char* targetStr_(FirmwareUpdateTarget t);
};
