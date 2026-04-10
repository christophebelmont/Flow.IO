#pragma once
/**
 * @file EventBusModule.h
 * @brief Active module hosting the EventBus task/dispatch loop.
 */
#include "Core/Module.h"
#include "Core/Services/Services.h"
#include "Core/EventBus/EventBus.h"

/**
 * @brief Active module that owns the EventBus instance.
 */
class EventBusModule : public Module {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::EventBus; }
    /** @brief Task name. */
    const char* taskName() const override { return "EventBus"; }
    /** @brief Pin control-path module on core 1. */
    BaseType_t taskCore() const override { return 1; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    /** @brief EventBus depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::LogHub;
        return ModuleId::Unknown;
    }
    /** @brief Initialize and register EventBus service. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Emit the startup event once every module finished config loading. */
    void onStart(ConfigStore& cfg, ServiceRegistry& services) override;
    /** @brief Dispatch events from the queue. */
    void loop() override;

    /** @brief Stack size override. */
    uint16_t taskStackSize() const override { return 2816; }
    /** @brief Task priority override. */
    UBaseType_t taskPriority() const override { return 1; }

private:
    EventBus _bus;
    EventBusService _svc { &_bus };

    const LogHubService* logHub = nullptr;
};
