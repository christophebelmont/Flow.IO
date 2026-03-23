#pragma once
/**
 * @file CommandModule.h
 * @brief Module that exposes the command registry service.
 */
#include "Core/ModulePassive.h"
#include "Core/CommandRegistry.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"

/**
 * @brief Passive module wiring command registration/execution service.
 */
class CommandModule : public ModulePassive {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::Command; }

    /** @brief Command module depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override { return (i == 0) ? ModuleId::LogHub : ModuleId::Unknown; }

    /** @brief Initialize registry and register service. */
    void init(ConfigStore&, ServiceRegistry& services) override;

private:
    CommandRegistry registry;
    const LogHubService* logHub = nullptr;

    bool registerHandler_(const char* cmd, CommandHandler fn, void* userCtx);
    bool execute_(const char* cmd, const char* json, const char* args, char* reply, size_t replyLen);

    CommandService svc_{
        ServiceBinding::bind<&CommandModule::registerHandler_>,
        ServiceBinding::bind<&CommandModule::execute_>,
        this
    };
};
