#pragma once
/**
 * @file SystemMonitorModule.h
 * @brief Periodic system health/metrics reporting module.
 */
#include "Core/Module.h"
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
    const char* moduleId() const override { return "sysmon"; }
    /** @brief Task name. */
    const char* taskName() const override { return "sysmon"; }
    /** @brief Pin monitoring module on core 0. */
    BaseType_t taskCore() const override { return 0; }

    // Only logger is mandatory
    /** @brief Depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        return nullptr;
    }

    /** @brief Initialize monitoring. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Monitoring loop. */
    void loop() override;

    // Needed to report task stack stats of all modules
    /** @brief Set ModuleManager for task stack reporting. */
    void setModuleManager(ModuleManager* mm) { moduleManager = mm; }

private:
    struct SystemMonitorConfig {
        bool traceEnabled = true;
        int32_t tracePeriodMs = 5000;
    };

    ModuleManager* moduleManager = nullptr;
    ConfigStore* cfgStore_ = nullptr;

    const WifiService* wifiSvc = nullptr;
    const ConfigStoreService* cfgSvc = nullptr;
    const LogHubService* logHub = nullptr;

    uint32_t lastStackDumpMs = 0;
    uint32_t lastJsonDumpMs = 0;
    uint32_t lastTraceLogMs = 0;

    void logBootInfo();
    void logHeapStats();
    void logTaskStacks();
    void buildHealthJson(char* out, size_t outLen);

    static const char* wifiStateStr(WifiState st);

    SystemMonitorConfig cfgData_{};
    ConfigVariable<bool,0> traceEnabledVar_{
        NVS_KEY(NvsKeys::SystemMonitor::TraceEnabled), "trace_enabled", "sysmon", ConfigType::Bool,
        &cfgData_.traceEnabled, ConfigPersistence::Persistent, 0
    };
    ConfigVariable<int32_t,0> tracePeriodVar_{
        NVS_KEY(NvsKeys::SystemMonitor::TracePeriodMs), "trace_period_ms", "sysmon", ConfigType::Int32,
        &cfgData_.tracePeriodMs, ConfigPersistence::Persistent, 0
    };
};
