#pragma once
/**
 * @file LogDispatcherModule.h
 * @brief Module that dispatches log entries to sinks.
 */
#include "Core/ModulePassive.h"
#include "Core/ServiceRegistry.h"
#include "Core/Services/ILogger.h"
#include "Core/LogHub.h"

/**
 * @brief Passive module that runs a task to consume log hub entries.
 */
class LogDispatcherModule : public ModulePassive {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::LogDispatcher; }
    uint8_t taskCount() const override { return (_hub && _sinkReg) ? 1 : 0; }
    const ModuleTaskSpec* taskSpecs() const override;

    /** @brief Depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }

    /** @brief Start dispatcher task and wire sink registry. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;

private:
    static void taskFn(void* pv);

    ServiceRegistry* _services = nullptr;
    LogHub* _hub = nullptr;
    const LogSinkRegistryService* _sinkReg = nullptr;
};
