#pragma once
/**
 * @file SystemMonitorModule.h
 * @brief Periodic system health/metrics reporting module.
 */
#include "Core/Module.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/Services/Services.h"
#include "Core/SystemStats.h"

// forward decl to avoid include dependency
class ModuleManager;

/**
 * @brief Active module that logs system metrics and health.
 */
class SystemMonitorModule : public Module {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::SystemMonitor; }
    /** @brief Task name. */
    const char* taskName() const override { return "sysmon"; }
    /** @brief Pin monitoring module on core 0. */
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 3584; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    // Only logger is mandatory
    /** @brief Depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }

    /** @brief Initialize monitoring. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;
    /** @brief Monitoring loop. */
    void loop() override;

    // Needed to report task stack stats of all modules
    /** @brief Set ModuleManager for task stack reporting. */
    void setModuleManager(ModuleManager* mm) { moduleManager = mm; }

private:
    struct SystemMonitorConfig {
        int32_t tracePeriodMs = 5000;
    };

    ModuleManager* moduleManager = nullptr;
    ConfigStore* cfgStore_ = nullptr;

    const WifiService* wifiSvc = nullptr;
    const ConfigStoreService* cfgSvc = nullptr;
    const LogHubService* logHub = nullptr;
    const HAService* haSvc_ = nullptr;
    bool haEntitiesRegistered_ = false;

    uint32_t lastJsonDumpMs = 0;
    uint32_t traceCycleStartMs_ = 0;
    bool bootInfoLogged_ = false;
    bool stackLoggedThisCycle_ = false;
    bool heapLoggedThisCycle_ = false;
    bool buffersLoggedThisCycle_ = false;
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    void logBootInfo();
    void logHeapStats();
    void logTaskStacks();
    void logTrackedBuffers();
    void buildHealthJson(char* out, size_t outLen);
    void registerHaEntities_(ServiceRegistry& services);

    static const char* wifiStateStr(WifiState st);

    SystemMonitorConfig cfgData_{};
    // CFGDOC: {"label":"Période trace système (ms)","help":"Période entre deux traces système.","unit":"ms"}
    ConfigVariable<int32_t,0> tracePeriodVar_{
        NVS_KEY(NvsKeys::SystemMonitor::TracePeriodMs), "trace_period_ms", "sysmon", ConfigType::Int32,
        &cfgData_.tracePeriodMs, ConfigPersistence::Persistent, 0
    };
};
