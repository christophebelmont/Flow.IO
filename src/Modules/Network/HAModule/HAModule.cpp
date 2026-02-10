/**
 * @file HAModule.cpp
 * @brief Implementation file.
 */

#include "HAModule.h"
#include "Modules/Network/HAModule/HARuntime.h"
#include <esp_system.h>
#include <ctype.h>
#include <string.h>

#define LOG_TAG "HAModule"
#include "Core/ModuleLog.h"

void HAModule::makeDeviceId(char* out, size_t len)
{
    if (!out || len == 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "ESP32-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void HAModule::makeHexNodeId(char* out, size_t len)
{
    if (!out || len == 0) return;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(out, len, "0x%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char* HAModule::skipWs(const char* p)
{
    while (p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}

void HAModule::sanitizeId(const char* in, char* out, size_t outLen)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!in) return;

    size_t w = 0;
    for (size_t i = 0; in[i] != '\0' && w + 1 < outLen; ++i) {
        char c = in[i];
        if (isalnum((unsigned char)c)) {
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            out[w++] = c;
        } else {
            out[w++] = '_';
        }
    }
    out[w] = '\0';
}

bool HAModule::nextModulePair(const char*& p, char* keyOut, size_t keyOutLen, JsonValueType& typeOut) const
{
    typeOut = JsonValueType::Unknown;
    if (!p || !keyOut || keyOutLen == 0) return false;

    p = skipWs(p);
    if (*p == '{') p = skipWs(p + 1);
    if (*p == ',') p = skipWs(p + 1);
    if (*p == '}' || *p == '\0') return false;
    if (*p != '"') return false;
    ++p;

    size_t k = 0;
    while (*p && *p != '"' && k + 1 < keyOutLen) {
        keyOut[k++] = *p++;
    }
    keyOut[k] = '\0';
    while (*p && *p != '"') ++p;
    if (*p != '"') return false;
    ++p;

    p = skipWs(p);
    if (*p != ':') return false;
    p = skipWs(p + 1);

    if (*p == '"') {
        typeOut = JsonValueType::String;
        ++p;
        while (*p) {
            if (*p == '\\' && *(p + 1)) {
                p += 2;
                continue;
            }
            if (*p == '"') {
                ++p;
                break;
            }
            ++p;
        }
    } else if (strncmp(p, "true", 4) == 0) {
        typeOut = JsonValueType::Bool;
        p += 4;
    } else if (strncmp(p, "false", 5) == 0) {
        typeOut = JsonValueType::Bool;
        p += 5;
    } else {
        typeOut = JsonValueType::Number;
        while (*p && *p != ',' && *p != '}') ++p;
    }
    return true;
}

bool HAModule::publishDiscovery(const char* component, const char* objectId, const char* payload)
{
    if (!component || !objectId || !payload || !mqttSvc || !mqttSvc->publish) return false;

    snprintf(topicBuf, sizeof(topicBuf), "%s/%s/%s/%s/config", cfgData.discoveryPrefix, component, nodeTopicId, objectId);
    return mqttSvc->publish(mqttSvc->ctx, topicBuf, payload, 1, true);
}

bool HAModule::publishSensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* entityCategory, const char* icon, const char* unit)
{
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;
    char unitField[48] = {0};
    if (unit && unit[0] != '\0') {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_measurement\":\"%s\"", unit);
    }

    if (entityCategory && entityCategory[0] != '\0' && icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"entity_category\":\"%s\",\"icon\":\"%s\"%s,"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, entityCategory, icon, unitField,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (entityCategory && entityCategory[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"entity_category\":\"%s\"%s,"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, entityCategory, unitField,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"icon\":\"%s\"%s,"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, icon, unitField,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\"%s,"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, unitField,
                 deviceIdent, cfgData.vendor, cfgData.model);
    }

    return publishDiscovery("sensor", objectId, payloadBuf);
}

bool HAModule::publishBinarySensor(const char* objectId, const char* name,
                                   const char* stateTopic, const char* valueTemplate,
                                   const char* deviceClass, const char* entityCategory,
                                   const char* icon)
{
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;

    if (deviceClass && deviceClass[0] != '\0' && entityCategory && entityCategory[0] != '\0' && icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"device_class\":\"%s\",\"entity_category\":\"%s\",\"icon\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, deviceClass, entityCategory, icon,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (deviceClass && deviceClass[0] != '\0' && entityCategory && entityCategory[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"device_class\":\"%s\",\"entity_category\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, deviceClass, entityCategory,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (deviceClass && deviceClass[0] != '\0' && icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"device_class\":\"%s\",\"icon\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, deviceClass, icon,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (deviceClass && deviceClass[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"device_class\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, deviceClass,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (entityCategory && entityCategory[0] != '\0' && icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"entity_category\":\"%s\",\"icon\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, entityCategory, icon,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (entityCategory && entityCategory[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"entity_category\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, entityCategory,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else if (icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"icon\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, icon,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate,
                 deviceIdent, cfgData.vendor, cfgData.model);
    }

    return publishDiscovery("binary_sensor", objectId, payloadBuf);
}

bool HAModule::publishSwitch(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic,
                             const char* payloadOn, const char* payloadOff,
                             const char* icon)
{
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !payloadOn || !payloadOff) return false;

    if (icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"state_on\":\"ON\",\"state_off\":\"OFF\","
                 "\"command_topic\":\"%s\",\"payload_on\":\"%s\",\"payload_off\":\"%s\","
                 "\"icon\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff, icon,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"state_on\":\"ON\",\"state_off\":\"OFF\","
                 "\"command_topic\":\"%s\",\"payload_on\":\"%s\",\"payload_off\":\"%s\","
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff,
                 deviceIdent, cfgData.vendor, cfgData.model);
    }
    return publishDiscovery("switch", objectId, payloadBuf);
}

bool HAModule::publishNumber(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic, const char* commandTemplate,
                             float minValue, float maxValue, float step,
                             const char* mode, const char* entityCategory, const char* icon, const char* unit)
{
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !commandTemplate) return false;

    char unitField[48] = {0};
    if (unit && unit[0] != '\0') {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_measurement\":\"%s\"", unit);
    }
    char entityCategoryField[48] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"command_topic\":\"%s\",\"command_template\":\"%s\","
                 "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"mode\":\"%s\",\"icon\":\"%s\"%s%s,"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 (double)minValue, (double)maxValue, (double)step, mode ? mode : "slider", icon, entityCategoryField, unitField,
                 deviceIdent, cfgData.vendor, cfgData.model);
    } else {
        snprintf(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"command_topic\":\"%s\",\"command_template\":\"%s\","
                 "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"mode\":\"%s\"%s%s,"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
                 name, objectId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 (double)minValue, (double)maxValue, (double)step, mode ? mode : "slider", entityCategoryField, unitField,
                 deviceIdent, cfgData.vendor, cfgData.model);
    }
    return publishDiscovery("number", objectId, payloadBuf);
}

const char* HAModule::iconForInput(uint8_t idx, const char* label) const
{
    if (idx == 0) return "mdi:ph";
    if (idx == 1) return "mdi:flash";
    if (idx == 2) return "mdi:gauge";
    if (idx == 3) return "mdi:water-thermometer";
    if (idx == 4) return "mdi:thermometer";
    if (!label) return "mdi:chart-line";
    if (strstr(label, "temp") || strstr(label, "Temp")) return "mdi:thermometer";
    return "mdi:chart-line";
}

const char* HAModule::unitForInput(uint8_t idx, const char* label) const
{
    if (idx == 1) return "mV";
    if (idx == 2) return "PSI";
    if (idx == 3 || idx == 4) return "°C";
    if (!label) return nullptr;
    if (strstr(label, "Temperature") || strstr(label, "Temp")) return "°C";
    if (strstr(label, "ORP")) return "mV";
    if (strstr(label, "PSI")) return "PSI";
    return nullptr;
}

const char* HAModule::iconForOutput(const char* label) const
{
    if (!label) return "mdi:toggle-switch-outline";
    if (strstr(label, "Filtration")) return "mdi:pool";
    if (strstr(label, "pH")) return "mdi:beaker-outline";
    if (strstr(label, "Chlorine Pump")) return "mdi:water-outline";
    if (strstr(label, "Generator")) return "mdi:flash";
    if (strstr(label, "Robot")) return "mdi:robot-vacuum";
    if (strstr(label, "Lights")) return "mdi:lightbulb";
    if (strstr(label, "Fill")) return "mdi:water-plus";
    if (strstr(label, "Heater")) return "mdi:water-boiler";
    return "mdi:toggle-switch-outline";
}

bool HAModule::readConfigString(const char* module, const char* key, char* out, size_t outLen)
{
    if (!module || !key || !out || outLen == 0 || !cfgSvc || !cfgSvc->toJsonModule) return false;
    bool truncated = false;
    if (!cfgSvc->toJsonModule(cfgSvc->ctx, module, moduleJsonBuf, sizeof(moduleJsonBuf), &truncated)) return false;

    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char* p = strstr(moduleJsonBuf, pattern);
    if (!p) return false;
    p += strlen(pattern);

    size_t w = 0;
    while (*p && *p != '"' && w + 1 < outLen) {
        out[w++] = *p++;
    }
    out[w] = '\0';
    (void)truncated;
    return w > 0;
}

bool HAModule::publishConfigStoreEntities()
{
    if (!cfgSvc || !cfgSvc->listModules || !cfgSvc->toJsonModule || !mqttSvc || !mqttSvc->formatTopic) return false;

    const char* modules[64] = {nullptr};
    uint8_t count = cfgSvc->listModules(cfgSvc->ctx, modules, 64);
    bool any = false;

    for (uint8_t i = 0; i < count; ++i) {
        const char* module = modules[i];
        if (!module || module[0] == '\0') continue;

        bool truncated = false;
        bool exists = cfgSvc->toJsonModule(cfgSvc->ctx, module, moduleJsonBuf, sizeof(moduleJsonBuf), &truncated);
        if (!exists) continue;

        snprintf(topicBuf, sizeof(topicBuf), "cfg/%s", module);
        mqttSvc->formatTopic(mqttSvc->ctx, topicBuf, stateTopicBuf, sizeof(stateTopicBuf));

        const char* p = moduleJsonBuf;
        char key[64];
        JsonValueType type = JsonValueType::Unknown;
        while (nextModulePair(p, key, sizeof(key), type)) {
            char idRaw[160];
            char objectId[160];
            char name[128];
            char valueTpl[128];
            snprintf(idRaw, sizeof(idRaw), "flowio_%s_cfg_%s_%s", deviceId, module, key);
            sanitizeId(idRaw, objectId, sizeof(objectId));
            snprintf(name, sizeof(name), "Cfg %s %s", module, key);
            snprintf(valueTpl, sizeof(valueTpl), "{{ value_json.%s }}", key);

            bool ok = false;
            if (type == JsonValueType::Bool) {
                ok = publishBinarySensor(objectId, name, stateTopicBuf, valueTpl, nullptr, "config");
            } else {
                ok = publishSensor(objectId, name, stateTopicBuf, valueTpl, "config");
            }
            any = any || ok;

            p = skipWs(p);
            if (*p == ',') ++p;
        }

        if (truncated) {
            LOGW("Config module JSON truncated for HA discovery (%s)", module);
        }
    }

    return any;
}

bool HAModule::publishDataStoreEntities()
{
    if (!mqttSvc || !mqttSvc->formatTopic || !cfgSvc || !cfgSvc->toJsonModule) return false;
    bool any = false;

    char objectId[160];
    char idRaw[160];

    for (uint8_t i = 0; i < 10; ++i) {
        char moduleName[32];
        char keyName[16];
        char label[24];
        snprintf(moduleName, sizeof(moduleName), "io/input/a%u", (unsigned)i);
        snprintf(keyName, sizeof(keyName), "a%u_name", (unsigned)i);
        if (!readConfigString(moduleName, keyName, label, sizeof(label))) continue;

        char stateSuffix[32];
        snprintf(stateSuffix, sizeof(stateSuffix), "rt/io/input/a%u", (unsigned)i);
        mqttSvc->formatTopic(mqttSvc->ctx, stateSuffix, stateTopicBuf, sizeof(stateTopicBuf));

        char tpl[64];
        snprintf(tpl, sizeof(tpl), "{{ value_json.value }}");

        snprintf(idRaw, sizeof(idRaw), "flowio_%s", label);
        sanitizeId(idRaw, objectId, sizeof(objectId));
        any = publishSensor(objectId, label, stateTopicBuf, tpl, nullptr, iconForInput(i, label), unitForInput(i, label)) || any;
    }

    char commandTopic[192];
    mqttSvc->formatTopic(mqttSvc->ctx, "cmd", commandTopic, sizeof(commandTopic));
    for (uint8_t i = 0; i < 10; ++i) {
        char moduleName[32];
        char keyName[16];
        char label[24];
        snprintf(moduleName, sizeof(moduleName), "io/output/d%u", (unsigned)i);
        snprintf(keyName, sizeof(keyName), "d%u_name", (unsigned)i);
        if (!readConfigString(moduleName, keyName, label, sizeof(label))) continue;

        char stateSuffix[32];
        snprintf(stateSuffix, sizeof(stateSuffix), "rt/io/output/d%u", (unsigned)i);
        mqttSvc->formatTopic(mqttSvc->ctx, stateSuffix, stateTopicBuf, sizeof(stateTopicBuf));

        char tpl[96];
        snprintf(tpl, sizeof(tpl), "{%% if value_json.value %%}ON{%% else %%}OFF{%% endif %%}");
        char payloadOn[96];
        char payloadOff[96];

        bool usePoolWrite = false;
        if (dsSvc && dsSvc->store && i < POOL_DEVICE_MAX) {
            const PoolDeviceRuntimeEntry& pd = dsSvc->store->data().pool.devices[i];
            usePoolWrite = pd.valid;
        }

        if (usePoolWrite) {
            snprintf(payloadOn, sizeof(payloadOn), "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd%u\\\",\\\"value\\\":true}}", (unsigned)i);
            snprintf(payloadOff, sizeof(payloadOff), "{\\\"cmd\\\":\\\"pool.write\\\",\\\"args\\\":{\\\"id\\\":\\\"pd%u\\\",\\\"value\\\":false}}", (unsigned)i);
        } else {
            snprintf(payloadOn, sizeof(payloadOn), "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d%u\\\",\\\"value\\\":true}}", (unsigned)i);
            snprintf(payloadOff, sizeof(payloadOff), "{\\\"cmd\\\":\\\"io.write\\\",\\\"args\\\":{\\\"id\\\":\\\"d%u\\\",\\\"value\\\":false}}", (unsigned)i);
        }

        snprintf(idRaw, sizeof(idRaw), "flowio_%s", label);
        sanitizeId(idRaw, objectId, sizeof(objectId));
        any = publishSwitch(objectId, label, stateTopicBuf, tpl, commandTopic, payloadOn, payloadOff, iconForOutput(label)) || any;
    }

    char cfgSetTopic[192];
    mqttSvc->formatTopic(mqttSvc->ctx, "cfg/set", cfgSetTopic, sizeof(cfgSetTopic));
    for (uint8_t i = 0; i < POOL_DEVICE_MAX; ++i) {
        char pdModule[24];
        snprintf(pdModule, sizeof(pdModule), "pdm/pd%u", (unsigned)i);
        bool truncated = false;
        bool pdExists = cfgSvc->toJsonModule(cfgSvc->ctx, pdModule, moduleJsonBuf, sizeof(moduleJsonBuf), &truncated);
        if (!pdExists) continue;

        char label[24];
        char ioModule[24];
        char ioKey[16];
        snprintf(ioModule, sizeof(ioModule), "io/output/d%u", (unsigned)i);
        snprintf(ioKey, sizeof(ioKey), "d%u_name", (unsigned)i);
        if (!readConfigString(ioModule, ioKey, label, sizeof(label))) {
            snprintf(label, sizeof(label), "Pool Device %u", (unsigned)i);
        }

        char cfgStateSuffix[32];
        snprintf(cfgStateSuffix, sizeof(cfgStateSuffix), "cfg/pdm/pd%u", (unsigned)i);
        mqttSvc->formatTopic(mqttSvc->ctx, cfgStateSuffix, stateTopicBuf, sizeof(stateTopicBuf));

        char idFlowRaw[160];
        char nameFlow[96];
        char valueTplFlow[64];
        char cmdTplFlow[128];
        snprintf(idFlowRaw, sizeof(idFlowRaw), "flowio_%s_flow_l_h", label);
        sanitizeId(idFlowRaw, objectId, sizeof(objectId));
        snprintf(nameFlow, sizeof(nameFlow), "%s Flowrate", label);
        snprintf(valueTplFlow, sizeof(valueTplFlow), "{{ value_json.flow_l_h }}");
        snprintf(cmdTplFlow, sizeof(cmdTplFlow), "{\\\"pdm/pd%u\\\":{\\\"flow_l_h\\\":{{ value | float(0) }}}}", (unsigned)i);
        any = publishNumber(objectId, nameFlow,
                            stateTopicBuf, valueTplFlow,
                            cfgSetTopic, cmdTplFlow,
                            0.0f, 3.0f, 0.1f,
                            "slider", "config", "mdi:water-sync", "L/h") || any;

    }

    return any;
}

bool HAModule::publishAutoconfig()
{
    return publishDataStoreEntities();
}

void HAModule::refreshIdentityFromConfig()
{
    if (cfgData.deviceId[0] != '\0') {
        strncpy(deviceId, cfgData.deviceId, sizeof(deviceId) - 1);
        deviceId[sizeof(deviceId) - 1] = '\0';
    } else {
        makeHexNodeId(deviceId, sizeof(deviceId));
    }
    sanitizeId(deviceId, nodeTopicId, sizeof(nodeTopicId));
    if (nodeTopicId[0] == '\0') {
        snprintf(nodeTopicId, sizeof(nodeTopicId), "flowio");
    }
    snprintf(deviceIdent, sizeof(deviceIdent), "%s-%s", cfgData.vendor, deviceId);
}

void HAModule::tryPublishAutoconfig()
{
    if (published) return;
    refreshIdentityFromConfig();
    if (!cfgData.enabled) return;
    if (!mqttSvc || !mqttSvc->isConnected || !dsSvc || !dsSvc->store) return;
    if (!mqttSvc->isConnected(mqttSvc->ctx)) return;
    if (!mqttReady(*dsSvc->store)) return;

    setHaVendor(*dsSvc->store, cfgData.vendor);
    setHaDeviceId(*dsSvc->store, deviceId);

    if (publishAutoconfig()) {
        published = true;
        setHaAutoconfigPublished(*dsSvc->store, true);
        LOGI("Home Assistant auto-discovery published");
    } else {
        setHaAutoconfigPublished(*dsSvc->store, false);
        LOGW("Home Assistant auto-discovery publish failed");
    }
}

void HAModule::onEventStatic(const Event& e, void* user)
{
    HAModule* self = static_cast<HAModule*>(user);
    if (self) self->onEvent(e);
}

void HAModule::onEvent(const Event& e)
{
    if (e.id != EventId::DataChanged) return;
    const DataChangedPayload* payload = static_cast<const DataChangedPayload*>(e.payload);
    if (!payload) return;
    if (!dsSvc || !dsSvc->store) return;

    if (payload->id == DATAKEY_WIFI_READY) {
        if (wifiReady(*dsSvc->store)) {
            signalAutoconfigCheck();
        }
        return;
    }

    if (payload->id == DATAKEY_MQTT_READY) {
        if (wifiReady(*dsSvc->store)) {
            signalAutoconfigCheck();
        }
        return;
    }
}

void HAModule::signalAutoconfigCheck()
{
    autoconfigPending = true;
    TaskHandle_t th = getTaskHandle();
    if (th) {
        xTaskNotifyGive(th);
    }
}

void HAModule::loop()
{
    if (!autoconfigPending) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    if (!autoconfigPending) return;
    autoconfigPending = false;
    tryPublishAutoconfig();
}

void HAModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    cfg.registerVar(enabledVar);
    cfg.registerVar(vendorVar);
    cfg.registerVar(deviceIdVar);
    cfg.registerVar(prefixVar);
    cfg.registerVar(modelVar);

    eventBusSvc = services.get<EventBusService>("eventbus");
    cfgSvc = services.get<ConfigStoreService>("config");
    dsSvc = services.get<DataStoreService>("datastore");
    mqttSvc = services.get<MqttService>("mqtt");

    if (dsSvc && dsSvc->store) {
        setHaAutoconfigPublished(*dsSvc->store, false);
    }

    if (eventBusSvc && eventBusSvc->bus) {
        eventBusSvc->bus->subscribe(EventId::DataChanged, &HAModule::onEventStatic, this);
    }

    if (dsSvc && dsSvc->store && wifiReady(*dsSvc->store)) {
        signalAutoconfigCheck();
    }
}
