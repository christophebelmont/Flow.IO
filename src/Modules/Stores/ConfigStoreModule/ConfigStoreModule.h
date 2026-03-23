#pragma once
/**
 * @file ConfigStoreModule.h
 * @brief Module that exposes ConfigStore service.
 */
#include "Core/ModulePassive.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"

/**
 * @brief Passive module wiring ConfigStore JSON services.
 */
class ConfigStoreModule : public ModulePassive {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::ConfigStore; }

    /** @brief Config module depends on log hub. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override { return (i == 0) ? ModuleId::LogHub : ModuleId::Unknown; }

    /** @brief Register config services. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;

private:
    ConfigStore* registry = nullptr;
    const LogHubService* logHub = nullptr;

    bool applyJson_(const char* json);
    void toJson_(char* out, size_t outLen);
    bool toJsonModule_(const char* module, char* out, size_t outLen, bool* truncated);
    uint8_t listModules_(const char** out, uint8_t max);
    bool erase_();

    ConfigStoreService svc_{
        ServiceBinding::bind<&ConfigStoreModule::applyJson_>,
        ServiceBinding::bind<&ConfigStoreModule::toJson_>,
        ServiceBinding::bind<&ConfigStoreModule::toJsonModule_>,
        ServiceBinding::bind<&ConfigStoreModule::listModules_>,
        ServiceBinding::bind<&ConfigStoreModule::erase_>,
        this
    };
};
