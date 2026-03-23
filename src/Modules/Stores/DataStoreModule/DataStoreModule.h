#pragma once
/**
 * @file DataStoreModule.h
 * @brief Module that exposes DataStore service.
 */
#include "Core/ModulePassive.h"
#include "Core/DataStore/DataStore.h"
#include "Core/Services/Services.h"

/**
 * @brief Passive module wiring the DataStore service.
 */
class DataStoreModule : public ModulePassive {
public:
    /** @brief Module id. */
    ModuleId moduleId() const override { return ModuleId::DataStore; }

    /** @brief DataStore depends on EventBus. */
    uint8_t dependencyCount() const override { return 1; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::EventBus;
        return ModuleId::Unknown;
    }

    /** @brief Initialize DataStore and register service. */
    void init(ConfigStore& cfg, ServiceRegistry& services) override;

private:
    DataStore _store;
    DataStoreService _svc{ &_store };
};
