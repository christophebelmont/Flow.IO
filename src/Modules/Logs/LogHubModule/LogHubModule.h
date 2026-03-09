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
    const char* moduleId() const override { return "loghub"; }

    /** @brief Initialize log hub and register services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;

private:
    LogHub hub;
    LogHubService hubSvc{};

    LogSinkRegistry sinks;
    LogSinkRegistryService sinksSvc{};
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;
};
