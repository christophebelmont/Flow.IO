#pragma once
/**
 * @file ModuleManager.h
 * @brief Dependency ordering and initialization for modules.
 */
#include "Module.h"

/** @brief Maximum number of modules supported at runtime. */
constexpr size_t MAX_MODULES = 25;

/**
 * @brief Registers modules, resolves dependencies, and starts tasks.
 */
class ModuleManager {
public:
    /** @brief Add a module to the manager. */
    void add(Module* m);
    /** @brief Initialize all modules in dependency order. */
    bool initAll(ConfigStore& cfg, ServiceRegistry& services);
    /** @brief Wire core services into the registry. */
    void wireCoreServices(ServiceRegistry& services, ConfigStore& config);
    
    /** @brief Task handle tracking for monitoring. */
    struct TaskEntry {
        const char* moduleId;
        TaskHandle_t handle;
    };

    /** @brief Current module count. */
    uint8_t getCount() const { return count; }
    /** @brief Get a module by index. */
    Module* getModule(uint8_t idx) const {
        if (idx >= count) return nullptr;
        return modules[idx];
    }
private:
    Module* modules[MAX_MODULES];
    uint8_t count = 0;

    Module* ordered[MAX_MODULES];
    uint8_t orderedCount = 0;

    Module* findById(const char* id);
    bool buildInitOrder();
};
