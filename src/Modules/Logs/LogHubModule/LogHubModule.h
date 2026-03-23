#pragma once
/**
 * @file LogHubModule.h
 * @brief Module that hosts the LogHub and sink registry.
 */
#include "Core/ModulePassive.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/ServiceRegistry.h"
#include "Core/LogHub.h"
#include "Core/LogSinkRegistry.h"
#include "Core/Services/ILogger.h"

/**
 * @brief Passive module wiring log hub and sink registry services.
 */
class LogHubModule : public ModulePassive {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::LogHub; }

    /** @brief Initialize log hub and register services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;

private:
    LogHub hub;
    LogHubService hubSvc_{};

    LogSinkRegistry sinks;
    LogSinkRegistryService sinksSvc_{};
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
};
