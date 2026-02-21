/**
 * @file ConfigStore.cpp
 * @brief Implementation file.
 */
#include "Core/ConfigStore.h"
#include "Core/NvsKeys.h"
#include "Core/Log.h"
#include "Core/SnprintfCheck.h"
#include <ArduinoJson.h>
#include <stdio.h>

#define LOG_TAG_CORE "CfgStore"
#undef snprintf
#define snprintf(OUT, LEN, FMT, ...) \
    FLOW_SNPRINTF_CHECKED(LOG_TAG_CORE, OUT, LEN, FMT, ##__VA_ARGS__)

static bool strEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

void ConfigStore::notifyChanged(const char* nvsKey, const char* moduleName, uint8_t moduleId, uint16_t branchId)
{
    if (!_eventBus || !nvsKey) return;

    ConfigChangedPayload p{};
    p.moduleId = moduleId;
    p.branchId = branchId;
    strncpy(p.nvsKey, nvsKey, sizeof(p.nvsKey) - 1);
    p.nvsKey[sizeof(p.nvsKey) - 1] = '\0';
    if (moduleName && moduleName[0] != '\0') {
        strncpy(p.module, moduleName, sizeof(p.module) - 1);
        p.module[sizeof(p.module) - 1] = '\0';
    }

    _eventBus->post(EventId::ConfigChanged, &p, sizeof(p));
}

void ConfigStore::recordNvsWrite_(size_t bytesWritten)
{
    if (bytesWritten == 0) return;
    _nvsWriteTotal.fetch_add(1U, std::memory_order_relaxed);
    _nvsWriteWindow.fetch_add(1U, std::memory_order_relaxed);
}

bool ConfigStore::putInt_(const char* key, int32_t value)
{
    if (!_prefs || !key) return false;
    const size_t wrote = _prefs->putInt(key, value);
    recordNvsWrite_(wrote);
    return wrote == sizeof(int32_t);
}

bool ConfigStore::putUChar_(const char* key, uint8_t value)
{
    if (!_prefs || !key) return false;
    const size_t wrote = _prefs->putUChar(key, value);
    recordNvsWrite_(wrote);
    return wrote == sizeof(uint8_t);
}

bool ConfigStore::putBool_(const char* key, bool value)
{
    if (!_prefs || !key) return false;
    const size_t wrote = _prefs->putBool(key, value);
    recordNvsWrite_(wrote);
    return wrote == sizeof(bool);
}

bool ConfigStore::putFloat_(const char* key, float value)
{
    if (!_prefs || !key) return false;
    const size_t wrote = _prefs->putFloat(key, value);
    recordNvsWrite_(wrote);
    return wrote == sizeof(float);
}

bool ConfigStore::putBytes_(const char* key, const void* value, size_t len)
{
    if (!_prefs || !key || !value || len == 0) return false;
    const size_t wrote = _prefs->putBytes(key, value, len);
    recordNvsWrite_(wrote);
    return wrote == len;
}

bool ConfigStore::putString_(const char* key, const char* value)
{
    if (!_prefs || !key || !value) return false;
    const size_t wrote = _prefs->putString(key, value);
    recordNvsWrite_(wrote);
    return (wrote > 0U) || (value[0] == '\0');
}

bool ConfigStore::putUInt_(const char* key, uint32_t value)
{
    if (!_prefs || !key) return false;
    const size_t wrote = _prefs->putUInt(key, value);
    recordNvsWrite_(wrote);
    return wrote == sizeof(uint32_t);
}

void ConfigStore::logNvsWriteSummaryIfDue(uint32_t nowMs, uint32_t periodMs)
{
    if (periodMs == 0U) return;

    const uint32_t last = _nvsLastSummaryMs.load(std::memory_order_relaxed);
    if (last == 0U) {
        _nvsLastSummaryMs.store(nowMs, std::memory_order_relaxed);
        return;
    }
    if ((uint32_t)(nowMs - last) < periodMs) return;

    _nvsLastSummaryMs.store(nowMs, std::memory_order_relaxed);
    const uint32_t windowWrites = _nvsWriteWindow.exchange(0U, std::memory_order_relaxed);
    const uint32_t totalWrites = _nvsWriteTotal.load(std::memory_order_relaxed);

    Log::info(LOG_TAG_CORE, "NVS writes: last_%lus=%lu total=%lu",
              (unsigned long)(periodMs / 1000U),
              (unsigned long)windowWrites,
              (unsigned long)totalWrites);
}

bool ConfigStore::writePersistent(const ConfigMeta& m)
{
    if (!_prefs) return false;
    if (m.persistence != ConfigPersistence::Persistent) return true;
    if (!m.nvsKey) return false;

    switch (m.type) {
        case ConfigType::Int32:
            return putInt_(m.nvsKey, *(int32_t*)m.valuePtr);
        case ConfigType::UInt8:
            return putUChar_(m.nvsKey, *(uint8_t*)m.valuePtr);
        case ConfigType::Bool:
            return putBool_(m.nvsKey, *(bool*)m.valuePtr);
        case ConfigType::Float:
            return putFloat_(m.nvsKey, *(float*)m.valuePtr);
        case ConfigType::Double:
            return putBytes_(m.nvsKey, m.valuePtr, sizeof(double));
        case ConfigType::CharArray:
            return putString_(m.nvsKey, (const char*)m.valuePtr);
        default:
            return false;
    }
}

void ConfigStore::loadPersistent()
{
    if (!_prefs) return;

    Log::debug(LOG_TAG_CORE, "loadPersistent: vars=%u", (unsigned)_metaCount);
    for (uint16_t i = 0; i < _metaCount; ++i) {
        ConfigMeta& m = _meta[i];
        if (m.persistence != ConfigPersistence::Persistent) continue;
        if (!m.nvsKey) continue;

        switch (m.type) {
            case ConfigType::Int32:
                *(int32_t*)m.valuePtr = _prefs->getInt(m.nvsKey, *(int32_t*)m.valuePtr);
                break;
            case ConfigType::UInt8:
                *(uint8_t*)m.valuePtr = _prefs->getUChar(m.nvsKey, *(uint8_t*)m.valuePtr);
                break;
            case ConfigType::Bool:
                *(bool*)m.valuePtr = _prefs->getBool(m.nvsKey, *(bool*)m.valuePtr);
                break;
            case ConfigType::Float:
                *(float*)m.valuePtr = _prefs->getFloat(m.nvsKey, *(float*)m.valuePtr);
                break;
            case ConfigType::Double: {
                double tmp = *(double*)m.valuePtr;
                _prefs->getBytes(m.nvsKey, &tmp, sizeof(double));
                *(double*)m.valuePtr = tmp;
                break;
            }
            case ConfigType::CharArray:
                _prefs->getString(m.nvsKey, (char*)m.valuePtr, m.size);
                break;
            default:
                break;
        }
    }
}

void ConfigStore::savePersistent()
{
    if (!_prefs) return;

    Log::debug(LOG_TAG_CORE, "savePersistent: vars=%u", (unsigned)_metaCount);
    for (uint16_t i = 0; i < _metaCount; ++i) {
        writePersistent(_meta[i]);
    }
}

bool ConfigStore::erasePersistent()
{
    if (!_prefs) return false;
    const bool ok = _prefs->clear();
    if (!ok) return false;

    _nvsWriteTotal.store(0U, std::memory_order_relaxed);
    _nvsWriteWindow.store(0U, std::memory_order_relaxed);
    _nvsLastSummaryMs.store(0U, std::memory_order_relaxed);
    return true;
}

const ConfigMeta* ConfigStore::findByJsonName(const char* jsonName) const
{
    if (!jsonName) return nullptr;
    for (uint16_t i = 0; i < _metaCount; ++i) {
        if (strEquals(_meta[i].name, jsonName)) return &_meta[i];
    }
    return nullptr;
}

ConfigMeta* ConfigStore::findByJsonName(const char* jsonName)
{
    return const_cast<ConfigMeta*>(
        static_cast<const ConfigStore*>(this)->findByJsonName(jsonName)
    );
}

static bool isMaskedKey(const char* key) {
    if (!key) return false;
    return strcmp(key, "pass") == 0 ||
           strcmp(key, "token") == 0 ||
           strcmp(key, "secret") == 0;
}

void ConfigStore::toJson(char* out, size_t outLen) const
{
    if (!out || outLen == 0) return;

    size_t pos = 0;
    out[pos++] = '{';

    for (uint16_t i = 0; i < _metaCount; ++i) {
        const ConfigMeta& m = _meta[i];

        if (i > 0) {
            if (pos + 1 >= outLen) break;
            out[pos++] = ',';
        }

        int n = snprintf(out + pos, outLen - pos, "\"%s\":", m.name ? m.name : "");
        if (n <= 0) break;
        pos += (size_t)n;
        if (pos >= outLen) break;

        switch (m.type) {
            case ConfigType::Int32:
                n = snprintf(out + pos, outLen - pos, "%ld", (long)*(int32_t*)m.valuePtr);
                break;
            case ConfigType::UInt8:
                n = snprintf(out + pos, outLen - pos, "%u", (unsigned)*(uint8_t*)m.valuePtr);
                break;
            case ConfigType::Bool:
                n = snprintf(out + pos, outLen - pos, "%s", (*(bool*)m.valuePtr) ? "true" : "false");
                break;
            case ConfigType::Float:
                n = snprintf(out + pos, outLen - pos, "%.3f", (double)*(float*)m.valuePtr);
                break;
            case ConfigType::Double:
                n = snprintf(out + pos, outLen - pos, "%.6f", *(double*)m.valuePtr);
                break;
            case ConfigType::CharArray:
                n = snprintf(out + pos, outLen - pos, "\"%s\"", (const char*)m.valuePtr);
                break;
            default:
                n = snprintf(out + pos, outLen - pos, "null");
                break;
        }

        if (n <= 0) break;
        pos += (size_t)n;
        if (pos >= outLen) break;
    }

    if (pos + 2 <= outLen) {
        out[pos++] = '}';
        out[pos] = '\0';
    } else {
        out[outLen - 1] = '\0';
    }
}

bool ConfigStore::toJsonModule(const char* module,
                               char* out,
                               size_t outLen,
                               bool* truncated,
                               bool maskSecrets) const
{
    if (!out || outLen == 0) return false;
    if (!module || module[0] == '\0') {
        out[0] = '\0';
        return false;
    }

    size_t pos = 0;
    out[pos++] = '{';

    bool any = false;
    bool truncatedLocal = false;
    for (uint16_t i = 0; i < _metaCount; ++i) {
        const ConfigMeta& m = _meta[i];
        if (!m.module || strcmp(m.module, module) != 0) continue;

        if (any) {
            if (pos + 1 >= outLen) { truncatedLocal = true; break; }
            out[pos++] = ',';
        }

        const char* key = m.name ? m.name : "";
        const size_t keyLen = strlen(key);
        if (pos + keyLen + 3 >= outLen) { // quotes + colon + trailing '\0'
            truncatedLocal = true;
            break;
        }
        out[pos++] = '"';
        memcpy(out + pos, key, keyLen);
        pos += keyLen;
        out[pos++] = '"';
        out[pos++] = ':';
        out[pos] = '\0';

        int n = 0;

        switch (m.type) {
            case ConfigType::Int32:
                n = snprintf(out + pos, outLen - pos, "%ld", (long)*(int32_t*)m.valuePtr);
                break;
            case ConfigType::UInt8:
                n = snprintf(out + pos, outLen - pos, "%u", (unsigned)*(uint8_t*)m.valuePtr);
                break;
            case ConfigType::Bool:
                n = snprintf(out + pos, outLen - pos, "%s", (*(bool*)m.valuePtr) ? "true" : "false");
                break;
            case ConfigType::Float:
                n = snprintf(out + pos, outLen - pos, "%.3f", (double)*(float*)m.valuePtr);
                break;
            case ConfigType::Double:
                n = snprintf(out + pos, outLen - pos, "%.6f", *(double*)m.valuePtr);
                break;
            case ConfigType::CharArray:
                if (maskSecrets && isMaskedKey(m.name)) {
                    n = snprintf(out + pos, outLen - pos, "\"***\"");
                } else {
                    n = snprintf(out + pos, outLen - pos, "\"%s\"", (const char*)m.valuePtr);
                }
                break;
            default:
                n = snprintf(out + pos, outLen - pos, "null");
                break;
        }

        if (n <= 0) break;
        pos += (size_t)n;
        if (pos >= outLen) { truncatedLocal = true; break; }

        any = true;
    }

    if (pos + 2 <= outLen) {
        out[pos++] = '}';
        out[pos] = '\0';
    } else {
        truncatedLocal = true;
        out[outLen - 1] = '\0';
    }

    if (truncated) *truncated = truncatedLocal;
    return any;
}

uint8_t ConfigStore::listModules(const char** out, uint8_t max) const
{
    if (!out || max == 0) return 0;
    uint8_t count = 0;

    for (uint16_t i = 0; i < _metaCount; ++i) {
        const ConfigMeta& m = _meta[i];
        if (!m.module || m.module[0] == '\0') continue;

        bool exists = false;
        for (uint8_t j = 0; j < count; ++j) {
            if (strcmp(out[j], m.module) == 0) { exists = true; break; }
        }
        if (exists) continue;

        if (count < max) {
            out[count++] = m.module;
        } else {
            break;
        }
    }

    return count;
}

bool ConfigStore::applyJson(const char* json)
{
    if (!json || json[0] == '\0') return false;

    static constexpr size_t APPLY_JSON_DOC_CAPACITY = Limits::JsonConfigApplyBuf;
    static StaticJsonDocument<APPLY_JSON_DOC_CAPACITY> doc;
    doc.clear();
    const DeserializationError err = deserializeJson(doc, json);
    if (err || !doc.is<JsonObjectConst>()) {
        Log::warn(LOG_TAG_CORE, "applyJson: invalid json");
        return false;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();

    Log::debug(LOG_TAG_CORE, "applyJson: start");
    for (uint16_t i = 0; i < _metaCount; ++i) {
        auto& m = _meta[i];
        if (!m.module || !m.name) continue;

        JsonVariantConst moduleVar = root[m.module];
        if (!moduleVar.is<JsonObjectConst>()) continue;
        JsonObjectConst moduleObj = moduleVar.as<JsonObjectConst>();

        JsonVariantConst valueVar = moduleObj[m.name];
        if (valueVar.isNull()) continue;

        bool changed = false;

        switch (m.type) {
        case ConfigType::Int32: {
            int32_t v = *(int32_t*)m.valuePtr;
            if (valueVar.is<int32_t>()) {
                v = valueVar.as<int32_t>();
            } else if (valueVar.is<uint32_t>()) {
                v = (int32_t)valueVar.as<uint32_t>();
            } else if (valueVar.is<const char*>()) {
                const char* s = valueVar.as<const char*>();
                if (!s) break;
                v = (int32_t)strtol(s, nullptr, 10);
            } else {
                break;
            }
            if (*(int32_t*)m.valuePtr != v) { *(int32_t*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::UInt8: {
            uint8_t v = *(uint8_t*)m.valuePtr;
            if (valueVar.is<uint8_t>()) {
                v = valueVar.as<uint8_t>();
            } else if (valueVar.is<uint16_t>()) {
                uint16_t u = valueVar.as<uint16_t>();
                if (u > 255U) u = 255U;
                v = (uint8_t)u;
            } else if (valueVar.is<int32_t>()) {
                int32_t n = valueVar.as<int32_t>();
                if (n < 0) n = 0;
                if (n > 255) n = 255;
                v = (uint8_t)n;
            } else if (valueVar.is<const char*>()) {
                const char* s = valueVar.as<const char*>();
                if (!s) break;
                long n = strtol(s, nullptr, 10);
                if (n < 0) n = 0;
                if (n > 255) n = 255;
                v = (uint8_t)n;
            } else {
                break;
            }
            if (*(uint8_t*)m.valuePtr != v) { *(uint8_t*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::Bool: {
            bool v = *(bool*)m.valuePtr;
            if (valueVar.is<bool>()) {
                v = valueVar.as<bool>();
            } else if (valueVar.is<int32_t>() || valueVar.is<uint32_t>() || valueVar.is<float>()) {
                v = (valueVar.as<float>() != 0.0f);
            } else if (valueVar.is<const char*>()) {
                const char* s = valueVar.as<const char*>();
                if (!s) break;
                v = (strcmp(s, "true") == 0 || strcmp(s, "1") == 0);
            } else {
                break;
            }
            if (*(bool*)m.valuePtr != v) { *(bool*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::Float: {
            float v = *(float*)m.valuePtr;
            if (valueVar.is<float>() || valueVar.is<double>() || valueVar.is<int32_t>() || valueVar.is<uint32_t>()) {
                v = valueVar.as<float>();
            } else if (valueVar.is<const char*>()) {
                const char* s = valueVar.as<const char*>();
                if (!s) break;
                v = (float)atof(s);
            } else {
                break;
            }
            if (*(float*)m.valuePtr != v) { *(float*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::Double: {
            double v = *(double*)m.valuePtr;
            if (valueVar.is<double>() || valueVar.is<float>() || valueVar.is<int32_t>() || valueVar.is<uint32_t>()) {
                v = valueVar.as<double>();
            } else if (valueVar.is<const char*>()) {
                const char* s = valueVar.as<const char*>();
                if (!s) break;
                v = atof(s);
            } else {
                break;
            }
            if (*(double*)m.valuePtr != v) { *(double*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::CharArray: {
            if (!valueVar.is<const char*>()) break;
            const char* s = valueVar.as<const char*>();
            if (!s) break;
            size_t len = strlen(s);
            if (len >= m.size) len = m.size - 1;

            /// compare before writing to avoid unnecessary events
            if (strncmp((char*)m.valuePtr, s, len) != 0 || ((char*)m.valuePtr)[len] != '\0') {
                memcpy(m.valuePtr, s, len);
                ((char*)m.valuePtr)[len] = '\0';
                changed = true;
            }
            break;
        }
        }

        if (changed) {
            Log::debug(LOG_TAG_CORE, "applyJson: changed %s.%s", m.module ? m.module : "-",
                       m.name ? m.name : "-");
            /// Save to NVS if needed
            if (m.persistence == ConfigPersistence::Persistent && m.nvsKey && _prefs) {
                switch (m.type) {
                case ConfigType::Int32:     putInt_(m.nvsKey, *(int32_t*)m.valuePtr); break;
                case ConfigType::UInt8:     putUChar_(m.nvsKey, *(uint8_t*)m.valuePtr); break;
                case ConfigType::Bool:      putBool_(m.nvsKey, *(bool*)m.valuePtr); break;
                case ConfigType::Float:     putFloat_(m.nvsKey, *(float*)m.valuePtr); break;
                case ConfigType::Double:    putBytes_(m.nvsKey, m.valuePtr, sizeof(double)); break;
                case ConfigType::CharArray: putString_(m.nvsKey, (char*)m.valuePtr); break;
                }
            }

            /// Notify listeners + EventBus
            if (m.nvsKey) {
                notifyChanged(m.nvsKey, m.module, m.moduleId, m.branchId);
            }
        }
    }
    Log::debug(LOG_TAG_CORE, "applyJson: done");
    return true;
}

bool ConfigStore::runMigrations(uint32_t currentVersion,
                                const MigrationStep* steps,
                                size_t count,
                                const char* versionKey,
                                bool clearOnFail)
{
    if (!_prefs || !steps || count == 0) return false;
    if (!versionKey) versionKey = NvsKeys::ConfigVersion;

    uint32_t storedVersion = _prefs->getUInt(versionKey, 0);
    Log::debug(LOG_TAG_CORE, "migrations: stored=%lu current=%lu",
               (unsigned long)storedVersion, (unsigned long)currentVersion);

    /// Déjà à jour
    if (storedVersion == currentVersion) return true;

    /// Version future (downgrade firmware par ex)
    if (storedVersion > currentVersion) {
        /// selon ton choix : refuser ou accepter
        return false;
    }

    while (storedVersion < currentVersion) {
        bool stepFound = false;

        for (size_t i = 0; i < count; ++i) {
            const MigrationStep& s = steps[i];
            if (s.fromVersion == storedVersion) {
                stepFound = true;

                if (!s.apply) return false;

                bool ok = s.apply(*_prefs, clearOnFail);
                if (!ok) {
                    Log::warn(LOG_TAG_CORE, "migration failed: %lu -> %lu",
                              (unsigned long)s.fromVersion, (unsigned long)s.toVersion);
                    if (clearOnFail) {
                        _prefs->clear();
                        putUInt_(versionKey, 0);
                    }
                    return false;
                }

                storedVersion = s.toVersion;
                putUInt_(versionKey, storedVersion);
                Log::debug(LOG_TAG_CORE, "migration applied: now=%lu", (unsigned long)storedVersion);
                break;
            }
        }

        if (!stepFound) {
            if (clearOnFail) {
                _prefs->clear();
                putUInt_(versionKey, 0);
            }
            return false;
        }
    }

    /// On garantit qu'on est bien à la version courante
    putUInt_(versionKey, currentVersion);
    Log::debug(LOG_TAG_CORE, "migrations: completed at %lu", (unsigned long)currentVersion);
    return true;
}
