#pragma once
/**
 * @file FirmwareUpdateModule.h
 * @brief Supervisor firmware updater (Flow.IO + Nextion).
 */

#include "Core/Module.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/ConfigTypes.h"
#include "Core/CommandRegistry.h"

struct BoardSpec;

class FirmwareUpdateModule : public Module {
public:
    explicit FirmwareUpdateModule(const BoardSpec& board);

    ModuleId moduleId() const override { return ModuleId::FirmwareUpdate; }
    const char* taskName() const override { return "fwupdate"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 8192; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        if (i == 1) return ModuleId::Wifi;
        if (i == 2) return ModuleId::Command;
        if (i == 3) return ModuleId::WebInterface;
        return ModuleId::Unknown;
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
        char cfgdocsPath[64] = "/webinterface/cfgdocs.fr.json";
    } cfgData_{};

    // CFGDOC: {"label":"Hôte serveur MAJ","help":"Adresse du serveur HTTP hébergeant les firmwares."}
    ConfigVariable<char, 2> updateHostVar_{
        NVS_KEY("up_host"), "update_host", "fwupdate",
        ConfigType::CharArray, cfgData_.updateHost, ConfigPersistence::Persistent, sizeof(cfgData_.updateHost)
    };
    // CFGDOC: {"label":"Chemin firmware Flow.IO","help":"Chemin du binaire firmware Flow.IO sur le serveur MAJ."}
    ConfigVariable<char, 2> flowioPathVar_{
        NVS_KEY("up_flow_path"), "flowio_path", "fwupdate",
        ConfigType::CharArray, cfgData_.flowioPath, ConfigPersistence::Persistent, sizeof(cfgData_.flowioPath)
    };
    // CFGDOC: {"label":"Chemin firmware Supervisor","help":"Chemin du binaire firmware Supervisor sur le serveur MAJ."}
    ConfigVariable<char, 2> supervisorPathVar_{
        NVS_KEY("up_sup_path"), "supervisor_path", "fwupdate",
        ConfigType::CharArray, cfgData_.supervisorPath, ConfigPersistence::Persistent, sizeof(cfgData_.supervisorPath)
    };
    // CFGDOC: {"label":"Chemin firmware Nextion","help":"Chemin du fichier TFT Nextion sur le serveur MAJ."}
    ConfigVariable<char, 2> nextionPathVar_{
        NVS_KEY("up_nx_path"), "nextion_path", "fwupdate",
        ConfigType::CharArray, cfgData_.nextionPath, ConfigPersistence::Persistent, sizeof(cfgData_.nextionPath)
    };
    // CFGDOC: {"label":"Chemin cfgdocs","help":"Chemin du fichier cfgdocs.fr.json sur le serveur MAJ."}
    ConfigVariable<char, 2> cfgdocsPathVar_{
        NVS_KEY("up_cfgdocs_path"), "cfgdocs_path", "fwupdate",
        ConfigType::CharArray, cfgData_.cfgdocsPath, ConfigPersistence::Persistent, sizeof(cfgData_.cfgdocsPath)
    };

    ServiceRegistry* services_ = nullptr;
    ConfigStore* cfgStore_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const WifiService* wifiSvc_ = nullptr;
    const WebInterfaceService* webInterfaceSvc_ = nullptr;

    int8_t flowIoEnablePin_ = -1;
    int8_t flowIoBootPin_ = -1;
    int8_t nextionRxPin_ = -1;
    int8_t nextionTxPin_ = -1;
    int8_t nextionRebootPin_ = -1;
    uint32_t nextionUploadBaud_ = 115200U;

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    UpdateJob queuedJob_{};
    UpdateStatus status_{};
    bool busy_ = false;
    uint32_t activeTotalBytes_ = 0;
    uint32_t activeSentBytes_ = 0;

    static bool cmdStatus_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdFlowIo_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdSupervisor_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdNextion_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);
    static bool cmdCfgDocs_(void* userCtx, const CommandRequest& req, char* reply, size_t replyLen);

    bool startUpdate_(FirmwareUpdateTarget target, const char* url, char* errOut, size_t errOutLen);
    bool statusJson_(char* out, size_t outLen);
    bool configJson_(char* out, size_t outLen) const;
    bool setConfig_(const char* updateHost,
                    const char* flowioPath,
                    const char* supervisorPath,
                    const char* nextionPath,
                    const char* cfgdocsPath,
                    char* errOut,
                    size_t errOutLen);
    bool runJob_(const UpdateJob& job);
    bool runFlowIoUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runSupervisorUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runNextionUpdate_(const char* url, char* errOut, size_t errOutLen);
    bool runCfgDocsUpdate_(const char* url, char* errOut, size_t errOutLen);
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

    FirmwareUpdateService firmwareUpdateSvc_{
        ServiceBinding::bind<&FirmwareUpdateModule::startUpdate_>,
        ServiceBinding::bind<&FirmwareUpdateModule::statusJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::configJson_>,
        ServiceBinding::bind<&FirmwareUpdateModule::setConfig_>,
        this
    };
};
