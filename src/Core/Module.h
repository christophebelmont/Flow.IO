#pragma once
/**
 * @file Module.h
 * @brief Base interface for all runtime modules.
 */
#include "ConfigStore.h"
#include "Runtime.h"
#include "ServiceRegistry.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Base class for active modules backed by a FreeRTOS task.
 */
class Module {
public:
    /** @brief Virtual destructor. */
    virtual ~Module() = default;

    /** @brief Unique module identifier (used for dependency wiring). */
    virtual const char* moduleId() const = 0;
    /** @brief FreeRTOS task name for this module. */
    virtual const char* taskName() const = 0;

    /** @brief Number of declared dependencies. */
    virtual uint8_t dependencyCount() const { return 0; }
    /** @brief Dependency id at index, or nullptr if none. */
    virtual const char* dependency(uint8_t) const { return nullptr; }

    /** @brief Initialize module and register services/config. */
    virtual void init(ConfigStore& cfg, ServiceRegistry& services) = 0;
    /** @brief Called once all persistent config values are loaded. */
    virtual void onConfigLoaded(ConfigStore&, ServiceRegistry&) {}
    /** @brief Main module loop called from the module task. */
    virtual void loop() = 0;

    /** @brief Stack size for the FreeRTOS task. */
    virtual uint16_t taskStackSize() const { return 3072; }
    /** @brief Task priority for the FreeRTOS task. */
    virtual UBaseType_t taskPriority() const { return 1; }

    /** @brief Create and start the FreeRTOS task for this module. */
    void startTask() {
        xTaskCreatePinnedToCore(
            taskEntry, taskName(), taskStackSize(),
            this, taskPriority(), &taskHandle, 1
        );
    }

    /** @brief Get the FreeRTOS task handle for this module. */
    TaskHandle_t getTaskHandle() const { return taskHandle; }

    /** @brief Whether this module owns a task. */
    virtual bool hasTask() const { return true; }
    
protected:
    TaskHandle_t taskHandle = nullptr;

private:
    static void taskEntry(void* arg) {
        Module* self = static_cast<Module*>(arg);
        while (true) {
            self->loop();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
};
