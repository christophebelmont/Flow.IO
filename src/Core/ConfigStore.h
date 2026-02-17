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
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <type_traits>

#include "ConfigTypes.h"
#include "Core/NvsKeys.h"
#include "Core/Log.h"
#include "Core/SystemLimits.h"
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
    static constexpr size_t JSON_BUFFER_SIZE = Limits::JsonCfgBuf;
    static constexpr size_t MAX_CONFIG_VARS = Limits::MaxConfigVars;

    //explicit ConfigStore(Preferences& prefs);
    ConfigStore() = default;

    /** @brief Inject EventBus dependency for change notifications. */
    void setEventBus(EventBus* bus) { _eventBus = bus; }
    /** @brief Inject Preferences for NVS persistence. */
    void setPreferences(Preferences& prefs) { _prefs = &prefs; }

    /** @brief Register a config variable definition. */
    template<typename T, size_t H>
    void registerVar(ConfigVariable<T, H>& var);
    /** @brief Register a config variable with explicit module/branch ids. */
    template<typename T, size_t H>
    void registerVar(ConfigVariable<T, H>& var, uint8_t moduleId, uint16_t branchId);

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
                       const char* versionKey = NvsKeys::ConfigVersion, bool clearOnFail = true);
    /** @brief Log NVS write summary when the configured period elapsed. */
    void logNvsWriteSummaryIfDue(uint32_t nowMs, uint32_t periodMs = 60000U);

private:
    Preferences* _prefs = nullptr;
    EventBus* _eventBus = nullptr;
    ConfigMeta _meta[MAX_CONFIG_VARS];
    uint16_t _metaCount = 0;
    bool _metaNearCapacityWarned = false;

    void notifyChanged(const char* nvsKey, const char* moduleName, uint8_t moduleId, uint16_t branchId);
    bool writePersistent(const ConfigMeta& m);
    void recordNvsWrite_(size_t bytesWritten);
    bool putInt_(const char* key, int32_t value);
    bool putUChar_(const char* key, uint8_t value);
    bool putBool_(const char* key, bool value);
    bool putFloat_(const char* key, float value);
    bool putBytes_(const char* key, const void* value, size_t len);
    bool putString_(const char* key, const char* value);
    bool putUInt_(const char* key, uint32_t value);

    const ConfigMeta* findByJsonName(const char* jsonName) const;
    ConfigMeta* findByJsonName(const char* jsonName);

    std::atomic<uint32_t> _nvsWriteTotal{0};
    std::atomic<uint32_t> _nvsWriteWindow{0};
    std::atomic<uint32_t> _nvsLastSummaryMs{0};
};

// -------------------------
// Template implementation
// -------------------------
template<typename T, size_t H>
void ConfigStore::registerVar(ConfigVariable<T, H>& var)
{
    if (_metaCount >= MAX_CONFIG_VARS) {
        Log::error(LOG_TAG_CORE,
                   "Config var capacity reached (%u), dropping module='%s' key='%s'",
                   (unsigned)MAX_CONFIG_VARS,
                   var.moduleName ? var.moduleName : "?",
                   var.nvsKey ? var.nvsKey : "?");
        return;
    }
    /*if (var.nvsKey && strlen(var.nvsKey) > MAX_NVS_KEY_LEN) {
        Log::warn(LOG_TAG_CORE, "NVS key too long (%s)", var.nvsKey);
        return;
    }*/

    // ✅ champ par champ (évite initializer list incompatible)
    ConfigMeta& m = _meta[_metaCount++];

    if (!_metaNearCapacityWarned) {
        const uint16_t warnAt = (uint16_t)((MAX_CONFIG_VARS * 9U) / 10U);
        if (_metaCount >= warnAt) {
            _metaNearCapacityWarned = true;
            Log::warn(LOG_TAG_CORE,
                      "Config var usage high: %u/%u",
                      (unsigned)_metaCount,
                      (unsigned)MAX_CONFIG_VARS);
        }
    }

    m.module      = var.moduleName;
    m.name        = var.jsonName;
    m.nvsKey      = var.nvsKey;
    m.type        = var.type;
    m.persistence = var.persistence;
    m.valuePtr    = (void*)var.value;
    m.size        = var.size;
    m.moduleId    = var.moduleId;
    m.branchId    = var.branchId;
}

template<typename T, size_t H>
void ConfigStore::registerVar(ConfigVariable<T, H>& var, uint8_t moduleId, uint16_t branchId)
{
    var.moduleId = moduleId;
    var.branchId = branchId;
    registerVar(var);
}

template<typename T, size_t H>
bool ConfigStore::set(ConfigVariable<T, H>& var, const T& value)
{
    if (!var.value) return false;

    bool changed = false;
    const T oldValue = *(var.value);

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

    // persist
    if (var.persistence == ConfigPersistence::Persistent && var.nvsKey && _prefs) {
        bool persisted = true;
        switch (var.type) {
        case ConfigType::Int32:  persisted = putInt_(var.nvsKey, *(int32_t*)var.value); break;
        case ConfigType::UInt8:  persisted = putUChar_(var.nvsKey, *(uint8_t*)var.value); break;
        case ConfigType::Bool:   persisted = putBool_(var.nvsKey, *(bool*)var.value); break;
        case ConfigType::Float:  persisted = putFloat_(var.nvsKey, *(float*)var.value); break;
        case ConfigType::Double: persisted = putBytes_(var.nvsKey, var.value, sizeof(double)); break;
        default: persisted = false; break;
        }
        if (!persisted) {
            *(var.value) = oldValue;
            return false;
        }
    }

    // notify handlers
    var.notify();

    // EventBus
    notifyChanged(var.nvsKey, var.moduleName, var.moduleId, var.branchId);

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

    if (var.persistence == ConfigPersistence::Persistent && var.nvsKey && _prefs) {
        if (!putString_(var.nvsKey, var.value)) {
            // Keep memory and NVS consistent when persistence fails.
            _prefs->getString(var.nvsKey, var.value, var.size);
            return false;
        }
    }

    var.notify();
    notifyChanged(var.nvsKey, var.moduleName, var.moduleId, var.branchId);
    return true;
}

#ifdef LOG_TAG_CORE_LOCAL_DEFINED
#undef LOG_TAG_CORE
#undef LOG_TAG_CORE_LOCAL_DEFINED
#endif
