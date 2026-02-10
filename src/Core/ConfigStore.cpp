/**
 * @file ConfigStore.cpp
 * @brief Implementation file.
 */
#include "Core/ConfigStore.h"
#include "Core/Log.h"
#include <stdio.h>

#define LOG_TAG_CORE "CfgStore"

static bool strEquals(const char* a, const char* b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

static const char* skipWs(const char* p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    return p;
}

static const char* findJsonValueStart(const char* json, const char* module, const char* name) {
    if (!json || !module || !name) return nullptr;

    char modPat[64];
    snprintf(modPat, sizeof(modPat), "\"%s\"", module);
    const char* p = strstr(json, modPat);
    if (!p) return nullptr;
    p += strlen(modPat);
    p = skipWs(p);
    if (!p || *p != ':') return nullptr;
    p = skipWs(p + 1);
    if (!p || *p != '{') return nullptr;
    p = skipWs(p + 1);

    char namePat[64];
    snprintf(namePat, sizeof(namePat), "\"%s\"", name);
    const char* q = strstr(p, namePat);
    if (!q) return nullptr;
    q += strlen(namePat);
    q = skipWs(q);
    if (!q || *q != ':') return nullptr;
    q = skipWs(q + 1);
    return q;
}

void ConfigStore::notifyChanged(const char* nvsKey)
{
    if (!_eventBus || !nvsKey) return;

    ConfigChangedPayload p{};
    strncpy(p.nvsKey, nvsKey, sizeof(p.nvsKey) - 1);
    p.nvsKey[sizeof(p.nvsKey) - 1] = '\0';

    _eventBus->post(EventId::ConfigChanged, &p, sizeof(p));
}

bool ConfigStore::writePersistent(const ConfigMeta& m)
{
    if (!_prefs) return false;
    if (m.persistence != ConfigPersistence::Persistent) return true;
    if (!m.nvsKey) return false;

    switch (m.type) {
        case ConfigType::Int32:
            _prefs->putInt(m.nvsKey, *(int32_t*)m.valuePtr);
            return true;
        case ConfigType::UInt8:
            _prefs->putUChar(m.nvsKey, *(uint8_t*)m.valuePtr);
            return true;
        case ConfigType::Bool:
            _prefs->putBool(m.nvsKey, *(bool*)m.valuePtr);
            return true;
        case ConfigType::Float:
            _prefs->putFloat(m.nvsKey, *(float*)m.valuePtr);
            return true;
        case ConfigType::Double:
            _prefs->putBytes(m.nvsKey, m.valuePtr, sizeof(double));
            return true;
        case ConfigType::CharArray:
            _prefs->putString(m.nvsKey, (const char*)m.valuePtr);
            return true;
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

bool ConfigStore::toJsonModule(const char* module, char* out, size_t outLen, bool* truncated) const
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

        int n = snprintf(out + pos, outLen - pos, "\"%s\":", m.name ? m.name : "");
        if (n <= 0) break;
        pos += (size_t)n;
        if (pos >= outLen) { truncatedLocal = true; break; }

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
                if (isMaskedKey(m.name)) {
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
    Log::debug(LOG_TAG_CORE, "applyJson: start");
    for (uint16_t i = 0; i < _metaCount; ++i) {
        auto& m = _meta[i];
        const char* p = findJsonValueStart(json, m.module, m.name);
        if (!p) continue;

        bool changed = false;

        switch (m.type) {
        case ConfigType::Int32: {
            int32_t v = atoi(p);
            if (*(int32_t*)m.valuePtr != v) { *(int32_t*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::UInt8: {
            uint8_t v = (uint8_t)atoi(p);
            if (*(uint8_t*)m.valuePtr != v) { *(uint8_t*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::Bool: {
            bool v = (strncmp(p, "true", 4) == 0);
            if (*(bool*)m.valuePtr != v) { *(bool*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::Float: {
            float v = (float)atof(p);
            if (*(float*)m.valuePtr != v) { *(float*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::Double: {
            double v = atof(p);
            if (*(double*)m.valuePtr != v) { *(double*)m.valuePtr = v; changed = true; }
            break;
        }
        case ConfigType::CharArray: {
            const char* s = strchr(p, '"'); if (!s) break; s++;
            const char* e = strchr(s, '"'); if (!e) break;
            size_t len = (size_t)(e - s);
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
                case ConfigType::Int32:     _prefs->putInt(m.nvsKey, *(int32_t*)m.valuePtr); break;
                case ConfigType::UInt8:     _prefs->putUChar(m.nvsKey, *(uint8_t*)m.valuePtr); break;
                case ConfigType::Bool:      _prefs->putBool(m.nvsKey, *(bool*)m.valuePtr); break;
                case ConfigType::Float:     _prefs->putFloat(m.nvsKey, *(float*)m.valuePtr); break;
                case ConfigType::Double:    _prefs->putBytes(m.nvsKey, m.valuePtr, sizeof(double)); break;
                case ConfigType::CharArray: _prefs->putString(m.nvsKey, (char*)m.valuePtr); break;
                }
            }

            /// Notify listeners + EventBus
            if (m.nvsKey) {
                notifyChanged(m.nvsKey);
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
    if (!versionKey) versionKey = "cfg_ver";

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
                        _prefs->putUInt(versionKey, 0);
                    }
                    return false;
                }

                storedVersion = s.toVersion;
                _prefs->putUInt(versionKey, storedVersion);
                Log::debug(LOG_TAG_CORE, "migration applied: now=%lu", (unsigned long)storedVersion);
                break;
            }
        }

        if (!stepFound) {
            if (clearOnFail) {
                _prefs->clear();
                _prefs->putUInt(versionKey, 0);
            }
            return false;
        }
    }

    /// On garantit qu'on est bien à la version courante
    _prefs->putUInt(versionKey, currentVersion);
    Log::debug(LOG_TAG_CORE, "migrations: completed at %lu", (unsigned long)currentVersion);
    return true;
}
