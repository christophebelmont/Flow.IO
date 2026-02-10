#pragma once
/**
 * @file ConfigStore.h
 * @brief Persistent configuration store with JSON import/export.
 */

// ConfigStore = persistent configuration store (previously ConfigRegistry).
//
// This version integrates with the EventBus:
// - When a config value changes (set() or applyJson()), it posts an
//   EventId::ConfigChanged event.
//
// Design goals (ESP32):
// - no heap allocations in normal runtime path
// - post() to EventBus is thread-safe (queue-based EventBus)

#include <Preferences.h>
#include <cstring>
#include <cstdlib>
#include <type_traits>

#include "ConfigTypes.h"
#include "Core/Log.h"
#include "Core/Services/IEventBus.h"
#include "Core/EventBus/EventBus.h"
#include "Core/EventBus/EventPayloads.h"

#ifndef LOG_TAG_CORE
#define LOG_TAG_CORE "CfgStore"
#define LOG_TAG_CORE_LOCAL_DEFINED
#endif

/** @brief Defines a configuration migration step between versions. */
struct MigrationStep {
    uint32_t fromVersion;
    uint32_t toVersion;
    bool (*apply)(Preferences& prefs, bool clearOnFail);
};

/**
 * @brief Holds config variables, persistence, and JSON import/export.
 */
class ConfigStore {
public:
    static constexpr size_t JSON_BUFFER_SIZE = 1024;
    static constexpr size_t MAX_CONFIG_VARS = 500;

    //explicit ConfigStore(Preferences& prefs);
    ConfigStore() = default;

    /** @brief Inject EventBus dependency for change notifications. */
    void setEventBus(EventBus* bus) { _eventBus = bus; }
    /** @brief Inject Preferences for NVS persistence. */
    void setPreferences(Preferences& prefs) { _prefs = &prefs; }

    /** @brief Register a config variable definition. */
    template<typename T, size_t H>
    void registerVar(ConfigVariable<T, H>& var);

    /** @brief Set a typed config value and persist if needed. */
    template<typename T, size_t H>
    bool set(ConfigVariable<T, H>& var, const T& value);

    /** @brief Set a char array config value and persist if needed. */
    template<size_t H>
    bool set(ConfigVariable<char, H>& var, const char* str);

    /** @brief Load persistent values from NVS into registered variables. */
    void loadPersistent();
    /** @brief Save persistent values from registered variables into NVS. */
    void savePersistent();

    /** @brief Serialize all registered config to JSON. */
    void toJson(char* out, size_t outLen) const;
    /** @brief Serialize a single module's config (flat object). */
    bool toJsonModule(const char* module, char* out, size_t outLen, bool* truncated = nullptr) const;
    /** @brief List unique module names present in config metadata. */
    uint8_t listModules(const char** out, uint8_t max) const;
    /** @brief Apply JSON patch to registered config variables. */
    bool applyJson(const char* json);

    /** @brief Run config migrations using a version key in NVS. */
    bool runMigrations(uint32_t currentVersion, const MigrationStep* steps, size_t count,
                       const char* versionKey = "cfg_ver", bool clearOnFail = true);

private:
    Preferences* _prefs = nullptr;
    EventBus* _eventBus = nullptr;
    ConfigMeta _meta[MAX_CONFIG_VARS];
    uint16_t _metaCount = 0;

    void notifyChanged(const char* nvsKey);
    bool writePersistent(const ConfigMeta& m);

    const ConfigMeta* findByJsonName(const char* jsonName) const;
    ConfigMeta* findByJsonName(const char* jsonName);
};

// -------------------------
// Template implementation
// -------------------------
template<typename T, size_t H>
void ConfigStore::registerVar(ConfigVariable<T, H>& var)
{
    if (_metaCount >= MAX_CONFIG_VARS) return;
    if (var.nvsKey && strlen(var.nvsKey) > MAX_NVS_KEY_LEN) {
        Log::warn(LOG_TAG_CORE, "NVS key too long (%s)", var.nvsKey);
        return;
    }

    // ✅ champ par champ (évite initializer list incompatible)
    ConfigMeta& m = _meta[_metaCount++];

    m.module      = var.moduleName;
    m.name        = var.jsonName;
    m.nvsKey      = var.nvsKey;
    m.type        = var.type;
    m.persistence = var.persistence;
    m.valuePtr    = (void*)var.value;
    m.size        = var.size;
}

template<typename T, size_t H>
bool ConfigStore::set(ConfigVariable<T, H>& var, const T& value)
{
    if (!var.value) return false;

    bool changed = false;

    // Comparaison selon type
    if constexpr (std::is_same<T, char>::value) {
        // cas char array géré ailleurs (normalement ConfigVariable<char,...>)
        changed = true;
    } else {
        if (*(var.value) != value) {
            *(var.value) = value;
            changed = true;
        }
    }

    if (!changed) return true;

    // notify handlers
    var.notify();

    // persist
    if (var.persistence == ConfigPersistence::Persistent && var.nvsKey && _prefs) {
        switch (var.type) {
        case ConfigType::Int32:  _prefs->putInt(var.nvsKey, *(int32_t*)var.value); break;
        case ConfigType::UInt8:  _prefs->putUChar(var.nvsKey, *(uint8_t*)var.value); break;
        case ConfigType::Bool:   _prefs->putBool(var.nvsKey, *(bool*)var.value); break;
        case ConfigType::Float:  _prefs->putFloat(var.nvsKey, *(float*)var.value); break;
        case ConfigType::Double: _prefs->putBytes(var.nvsKey, var.value, sizeof(double)); break;
        default: break;
        }
    }

    // EventBus
    notifyChanged(var.nvsKey);

    return true;
}

template<size_t H>
bool ConfigStore::set(ConfigVariable<char, H>& var, const char* str)
{
    if (!var.value || !str || var.size == 0) return false;

    size_t len = strlen(str);
    if (len >= var.size) len = var.size - 1;

    bool changed = false;
    if (strncmp(var.value, str, len) != 0 || var.value[len] != '\0') {
        memcpy(var.value, str, len);
        var.value[len] = '\0';
        changed = true;
    }

    if (!changed) return true;

    var.notify();

    if (var.persistence == ConfigPersistence::Persistent && var.nvsKey && _prefs) {
        _prefs->putString(var.nvsKey, var.value);
    }

    notifyChanged(var.nvsKey);
    return true;
}

#ifdef LOG_TAG_CORE_LOCAL_DEFINED
#undef LOG_TAG_CORE
#undef LOG_TAG_CORE_LOCAL_DEFINED
#endif
