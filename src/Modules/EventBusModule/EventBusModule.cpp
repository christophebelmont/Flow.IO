/**
 * @file EventBusModule.cpp
 * @brief Implementation file.
 */
#include "EventBusModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::EventBusModule)
#include "Core/ModuleLog.h"


void EventBusModule::init(ConfigStore&, ServiceRegistry& services) {
    /// récupérer service loghub (log async)
    logHub = services.get<LogHubService>("loghub");

    services.add("eventbus", &_svc);

    LOGI("EventBusService registered");
    /// Broadcast system started (no payload)
    _bus.post(EventId::SystemStarted, nullptr, 0);
}

void EventBusModule::loop() {
    /// Dispatch queued events.
    _bus.dispatch(16);
    vTaskDelay(pdMS_TO_TICKS(5));
}
