#pragma once
/**
 * @file Module.h
 * @brief Base interface for all runtime modules.
 */
#include "ConfigStore.h"
#include "Core/ModuleId.h"
#include "Core/SystemLimits.h"
#include "Runtime.h"
#include "ServiceRegistry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Base class for active modules backed by a FreeRTOS task.
 */
struct ModuleTaskSpec {
    const char* name;
    uint32_t stackSize;
    UBaseType_t priority;
    BaseType_t coreId;
    TaskFunction_t entry;
    void* context;
};

class Module {
public:
    /** @brief Virtual destructor. */
    virtual ~Module() = default;

    /** @brief Unique module identifier (used for dependency wiring). */
    virtual ModuleId moduleId() const = 0;
    /** @brief FreeRTOS task name for this module. */
    virtual const char* taskName() const = 0;

    /** @brief Number of declared dependencies. */
    virtual uint8_t dependencyCount() const { return 0; }
    /** @brief Dependency id at index, or ModuleId::Unknown if none. */
    virtual ModuleId dependency(uint8_t) const { return ModuleId::Unknown; }

    /** @brief Initialize module and register services/config. */
    virtual void init(ConfigStore& cfg, ServiceRegistry& services) = 0;
    /** @brief Called once all persistent config values are loaded. */
    virtual void onConfigLoaded(ConfigStore&, ServiceRegistry&) {}
    /** @brief Main module loop called from the module task. */
    virtual void loop() = 0;

    /** @brief Number of declared FreeRTOS tasks owned by this module. */
    virtual uint8_t taskCount() const { return 0; }
    /** @brief Array of declared FreeRTOS tasks owned by this module. */
    virtual const ModuleTaskSpec* taskSpecs() const { return nullptr; }

    /** @brief Stack size for the FreeRTOS task. */
    virtual uint16_t taskStackSize() const { return Limits::Core::Task::DefaultStackSize; }
    /** @brief Task priority for the FreeRTOS task. */
    virtual UBaseType_t taskPriority() const { return 1; }
    /** @brief CPU core affinity for the FreeRTOS task (`0` or `1` on ESP32). */
    virtual BaseType_t taskCore() const { return 1; }

    /** @brief Get the first started FreeRTOS task handle for this module. */
    TaskHandle_t getTaskHandle() const { return primaryTaskHandle_; }

    /** @brief Whether this module owns at least one declared task. */
    virtual bool hasTask() const { return taskCount() > 0; }

protected:
    /** @brief Helper for classic "loop() in one task" modules. */
    const ModuleTaskSpec* singleLoopTaskSpec() const {
        static ModuleTaskSpec spec;
        spec = {
            taskName(),
            (uint32_t)taskStackSize(),
            taskPriority(),
            taskCore(),
            &Module::taskEntry,
            const_cast<Module*>(this)
        };
        return &spec;
    }

private:
    friend class ModuleManager;

    static void taskEntry(void* arg) {
        Module* self = static_cast<Module*>(arg);
        while (true) {
            self->loop();
            vTaskDelay(pdMS_TO_TICKS(Limits::Core::Timing::LoopDelayMs));
        }
    }

    void resetPrimaryTaskHandle_() { primaryTaskHandle_ = nullptr; }
    void setPrimaryTaskHandle_(TaskHandle_t handle) {
        if (!primaryTaskHandle_) primaryTaskHandle_ = handle;
    }

    TaskHandle_t primaryTaskHandle_ = nullptr;
};
