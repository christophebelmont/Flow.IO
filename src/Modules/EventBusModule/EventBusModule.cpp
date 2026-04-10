/**
 * @file EventBusModule.cpp
 * @brief Implementation file.
 */
#include "EventBusModule.h"
#include "Core/ModuleId.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::EventBusModule)
#include "Core/ModuleLog.h"


void EventBusModule::init(ConfigStore&, ServiceRegistry& services) {
    /// récupérer service loghub (log async)
    logHub = services.get<LogHubService>(ServiceId::LogHub);

    if (!services.add(ServiceId::EventBus, &_svc)) {
        LOGE("service registration failed: %s", toString(ServiceId::EventBus));
    }

    LOGI("EventBusService registered");
}

void EventBusModule::onStart(ConfigStore&, ServiceRegistry&)
{
    /// Broadcast system started after all modules completed config loading and subscriptions.
    _bus.post(EventId::SystemStarted, nullptr, 0, ModuleId::EventBus);
}

void EventBusModule::loop() {
    /// Dispatch queued events.
    _bus.dispatch(16);
    vTaskDelay(pdMS_TO_TICKS(5));
}
