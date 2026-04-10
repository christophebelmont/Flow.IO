#pragma once
/**
 * @file ModuleManager.h
 * @brief Dependency ordering and initialization for modules.
 */
#include "Module.h"

/**
 * @brief Registers modules, resolves dependencies, and orchestrates startup.
 *
 * Lifecycle driven by `ModuleManager`:
 *
 * 1. `init()`
 * 2. `ConfigStore::loadPersistent()`
 * 3. `onConfigLoaded()`
 * 4. non-blocking startup sequencing:
 *    - wait until `startDelayMs()` has elapsed relative to the startup epoch
 *    - ensure every declared dependency has already been released
 *    - call `onStart()`
 *    - create the module tasks, if any
 *
 * This allows both active modules and passive modules to defer their business
 * startup work without blocking the global boot path.
 *
 * Current explicit startup delays:
 *
 * | Module | Delay (ms) | Rationale |
 * | --- | ---: | --- |
 * | `eventbus` | `0` | Event bus available immediately; publishes `SystemStarted` from `onStart()` once subscriptions are in place |
 * | `mqtt` | `1500` | Preserves the historical staged release before MQTT connect attempts |
 * | `poollogic` | `10000` | Preserves the historical delayed control-loop start |
 * | `ha` | `15000` | Preserves the historical delayed Home Assistant discovery startup |
 * | `webinterface` | `10000` | Preserves the historical Supervisor web-server warm-up, now handled by `ModuleManager` |
 * | others | `0` | Immediate release after `onConfigLoaded()` unless the module overrides `startDelayMs()` |
 */
class ModuleManager {
public:
    /** @brief Add a module to the manager. */
    bool add(Module* m);
    /** @brief Initialize modules, load config, and prepare the startup sequencer. */
    bool initAll(ConfigStore& cfg, ServiceRegistry& services);
    /** @brief Advance the non-blocking startup sequencer and release due modules. */
    bool tickStartup(ConfigStore& cfg, ServiceRegistry& services);
    /** @brief Whether every registered module has been released by the startup sequencer. */
    bool startupComplete() const { return (startupFlags_ & 0x01U) != 0U && (startupFlags_ & 0x06U) == 0U; }
    /** @brief Wire core services into the registry. */
    void wireCoreServices(ServiceRegistry& services, ConfigStore& config);
    
    /** @brief Task handle tracking for monitoring. */
    struct TaskEntry {
        Module* module;
        TaskHandle_t handle;
        uint8_t taskIndex;
    };

    /** @brief Current module count. */
    uint8_t getCount() const { return count; }
    /** @brief Get a module by index. */
    Module* getModule(uint8_t idx) const {
        if (idx >= count) return nullptr;
        return modules[idx];
    }
    /** @brief Number of started module tasks. */
    uint8_t getTaskEntryCount() const { return taskEntryCount; }
    /** @brief Get a started task entry by index. */
    const TaskEntry* getTaskEntry(uint8_t idx) const {
        if (idx >= taskEntryCount) return nullptr;
        return &taskEntries[idx];
    }
private:
    Module* modules[Limits::Core::Capacity::MaxModules]{};
    Module* modulesById[kModuleIdCount]{};
    uint8_t count = 0;

    Module* ordered[Limits::Core::Capacity::MaxModules]{};
    uint8_t orderedCount = 0;
    TaskEntry taskEntries[Limits::Core::Capacity::MaxModuleTasks]{};
    uint8_t taskEntryCount = 0;
    uint32_t startupEpochMs_ = 0;
    uint32_t startupStartedMask_ = 0;
    uint8_t startupFlags_ = 0; // bit0: prepared, bit1: active, bit2: failed

    Module* findById(ModuleId id);
    bool buildInitOrder();
    bool dependenciesStarted_(Module& module) const;
    bool startModule_(uint8_t orderedIdx, ConfigStore& cfg, ServiceRegistry& services);
};
