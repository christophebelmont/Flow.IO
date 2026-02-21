/**
 * @file HAModule.cpp
 * @brief Implementation file.
 */

#include "HAModule.h"
#include "Modules/Network/HAModule/HARuntime.h"
#include "Core/MqttTopics.h"
#include "Core/SystemLimits.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#define LOG_TAG "HAModule"
#include "Core/ModuleLog.h"

#ifndef FIRMW
#define FIRMW "unknown"
#endif

static void buildAvailabilityField(const MqttService* mqttSvc,
                                   char* out,
                                   size_t outLen,
                                   const char* stateTopic = nullptr,
                                   const char* stateAvailabilityTemplate = nullptr)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!mqttSvc || !mqttSvc->formatTopic) return;

    char availabilityTopic[192] = {0};
    mqttSvc->formatTopic(mqttSvc->ctx, MqttTopics::SuffixStatus, availabilityTopic, sizeof(availabilityTopic));
    const bool hasStateAvailability =
        stateTopic && stateTopic[0] != '\0' &&
        stateAvailabilityTemplate && stateAvailabilityTemplate[0] != '\0';

    if (availabilityTopic[0] == '\0') {
        if (!hasStateAvailability) return;
        snprintf(
            out,
            outLen,
            ",\"availability\":[{\"topic\":\"%s\",\"value_template\":\"%s\"}],"
            "\"availability_mode\":\"all\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"",
            stateTopic,
            stateAvailabilityTemplate
        );
        return;
    }

    if (hasStateAvailability) {
        snprintf(
            out,
            outLen,
            ",\"availability\":[{\"topic\":\"%s\",\"value_template\":\"%s\"},"
            "{\"topic\":\"%s\",\"value_template\":\"{{ 'online' if value_json.online else 'offline' }}\"}],"
            "\"availability_mode\":\"all\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"",
            stateTopic,
            stateAvailabilityTemplate,
            availabilityTopic
        );
        return;
    }

    snprintf(
        out,
        outLen,
        ",\"availability\":[{\"topic\":\"%s\",\"value_template\":\"{{ 'online' if value_json.online else 'offline' }}\"}],"
        "\"availability_mode\":\"all\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\"",
        availabilityTopic
    );
}

static bool formatChecked(char* out, size_t outLen, const char* fmt, ...)
{
    if (!out || outLen == 0 || !fmt) return false;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out, outLen, fmt, ap);
    va_end(ap);
    return (n >= 0) && ((size_t)n < outLen);
}

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

uint16_t HAModule::hash3Digits(const char* in)
{
    // 32-bit FNV-1a reduced to 3 decimal digits for short per-device entity prefixes.
    uint32_t h = 2166136261u;
    const char* p = in ? in : "";
    while (*p) {
        h ^= (uint8_t)(*p++);
        h *= 16777619u;
    }
    return (uint16_t)(h % 1000u);
}

bool HAModule::svcAddSensor(void* ctx, const HASensorEntry* entry)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (!self || !entry) return false;
    return self->addSensorEntry(*entry);
}

bool HAModule::svcAddBinarySensor(void* ctx, const HABinarySensorEntry* entry)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (!self || !entry) return false;
    return self->addBinarySensorEntry(*entry);
}

bool HAModule::svcAddSwitch(void* ctx, const HASwitchEntry* entry)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (!self || !entry) return false;
    return self->addSwitchEntry(*entry);
}

bool HAModule::svcAddNumber(void* ctx, const HANumberEntry* entry)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (!self || !entry) return false;
    return self->addNumberEntry(*entry);
}

bool HAModule::svcAddButton(void* ctx, const HAButtonEntry* entry)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (!self || !entry) return false;
    return self->addButtonEntry(*entry);
}

bool HAModule::svcRequestRefresh(void* ctx)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (!self) return false;
    self->requestAutoconfigRefresh();
    return true;
}

bool HAModule::addSensorEntry(const HASensorEntry& entry)
{
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix || !entry.valueTemplate) {
        return false;
    }

    for (uint8_t i = 0; i < sensorCount_; ++i) {
        if (strcmp(sensors_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(sensors_[i].objectSuffix, entry.objectSuffix) == 0) {
            sensors_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (sensorCount_ >= MAX_HA_SENSORS) return false;
    sensors_[sensorCount_++] = entry;
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addBinarySensorEntry(const HABinarySensorEntry& entry)
{
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix || !entry.valueTemplate) {
        return false;
    }

    for (uint8_t i = 0; i < binarySensorCount_; ++i) {
        if (strcmp(binarySensors_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(binarySensors_[i].objectSuffix, entry.objectSuffix) == 0) {
            binarySensors_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (binarySensorCount_ >= MAX_HA_BINARY_SENSORS) return false;
    binarySensors_[binarySensorCount_++] = entry;
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addSwitchEntry(const HASwitchEntry& entry)
{
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix ||
        !entry.valueTemplate || !entry.commandTopicSuffix || !entry.payloadOn || !entry.payloadOff) {
        return false;
    }

    for (uint8_t i = 0; i < switchCount_; ++i) {
        if (strcmp(switches_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(switches_[i].objectSuffix, entry.objectSuffix) == 0) {
            switches_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (switchCount_ >= MAX_HA_SWITCHES) return false;
    switches_[switchCount_++] = entry;
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addNumberEntry(const HANumberEntry& entry)
{
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.stateTopicSuffix ||
        !entry.valueTemplate || !entry.commandTopicSuffix || !entry.commandTemplate) {
        return false;
    }

    for (uint8_t i = 0; i < numberCount_; ++i) {
        if (strcmp(numbers_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(numbers_[i].objectSuffix, entry.objectSuffix) == 0) {
            numbers_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (numberCount_ >= MAX_HA_NUMBERS) return false;
    numbers_[numberCount_++] = entry;
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::addButtonEntry(const HAButtonEntry& entry)
{
    if (!entry.ownerId || !entry.objectSuffix || !entry.name || !entry.commandTopicSuffix || !entry.payloadPress) {
        return false;
    }

    for (uint8_t i = 0; i < buttonCount_; ++i) {
        if (strcmp(buttons_[i].ownerId, entry.ownerId) == 0 &&
            strcmp(buttons_[i].objectSuffix, entry.objectSuffix) == 0) {
            buttons_[i] = entry;
            requestAutoconfigRefresh();
            return true;
        }
    }

    if (buttonCount_ >= MAX_HA_BUTTONS) return false;
    buttons_[buttonCount_++] = entry;
    requestAutoconfigRefresh();
    return true;
}

bool HAModule::buildObjectId(const char* suffix, char* out, size_t outLen) const
{
    if (!suffix || !out || outLen == 0) return false;
    char raw[256] = {0};
    snprintf(raw, sizeof(raw), "flowio%03u_%s", (unsigned)entityHash3_, suffix);
    sanitizeId(raw, out, outLen);
    return out[0] != '\0';
}

bool HAModule::buildDefaultEntityId(const char* component, const char* objectId, char* out, size_t outLen) const
{
    if (!component || !objectId || !out || outLen == 0) return false;
    snprintf(out, outLen, "%s.%s", component, objectId);
    return out[0] != '\0';
}

bool HAModule::buildUniqueId(const char* objectId, const char* name, char* out, size_t outLen) const
{
    if (!objectId || !out || outLen == 0) return false;
    char cleanName[96] = {0};
    sanitizeId(name ? name : "", cleanName, sizeof(cleanName));
    if (cleanName[0] != '\0') {
        snprintf(out, outLen, "%s_%s_%s", deviceId, objectId, cleanName);
    } else {
        snprintf(out, outLen, "%s_%s", deviceId, objectId);
    }
    return out[0] != '\0';
}

bool HAModule::publishDiscovery(const char* component, const char* objectId, const char* payload)
{
    if (!component || !objectId || !payload || !mqttSvc || !mqttSvc->publish) return false;

    const uint32_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (freeHeap < MIN_FREE_HEAP_FOR_PUBLISH || largest < MIN_LARGEST_BLOCK_FOR_PUBLISH) {
        const uint32_t now = millis();
        if ((now - lastLowHeapLogMs_) >= 2000U) {
            lastLowHeapLogMs_ = now;
            LOGW("Skip HA discovery publish (low heap free=%lu largest=%lu need_free=%lu need_largest=%lu)",
                 (unsigned long)freeHeap,
                 (unsigned long)largest,
                 (unsigned long)MIN_FREE_HEAP_FOR_PUBLISH,
                 (unsigned long)MIN_LARGEST_BLOCK_FOR_PUBLISH);
        }
        return false;
    }

    if (!formatChecked(topicBuf, sizeof(topicBuf), "%s/%s/%s/%s/config", cfgData.discoveryPrefix, component, nodeTopicId, objectId)) {
        LOGW("HA discovery topic truncated component=%s object=%s", component, objectId);
        return false;
    }
    // QoS0 lowers transient heap pressure while keeping retained discovery on broker.
    return mqttSvc->publish(mqttSvc->ctx, topicBuf, payload, 0, true);
}

bool HAModule::publishSensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* entityCategory, const char* icon, const char* unit,
                             bool hasEntityName,
                             const char* availabilityTemplate)
{
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;

    char unitField[64] = {0};
    if (unit) {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_measurement\":\"%s\"", unit);
    }
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }
    char iconField[64] = {0};
    if (icon && icon[0] != '\0') {
        snprintf(iconField, sizeof(iconField), ",\"icon\":\"%s\"", icon);
    }
    (void)hasEntityName;
    const char* hasEntityNameField = ",\"has_entity_name\":false";
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("sensor", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[768] = {0};
    buildAvailabilityField(mqttSvc, availabilityField, sizeof(availabilityField), stateTopic, availabilityTemplate);

    if (!formatChecked(payloadBuf, sizeof(payloadBuf),
             "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
             "\"state_topic\":\"%s\",\"value_template\":\"%s\",\"state_class\":\"measurement\"%s%s%s%s%s,"
             "\"origin\":{\"name\":\"Flow.IO\"},"
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
             "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
             name, objectId, defaultEntityId, uniqueId,
             stateTopic, valueTemplate,
             entityCategoryField, iconField, unitField, hasEntityNameField, availabilityField,
             deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
        LOGW("HA sensor payload truncated object=%s", objectId);
        return false;
    }

    return publishDiscovery("sensor", objectId, payloadBuf);
}

bool HAModule::publishBinarySensor(const char* objectId, const char* name,
                                   const char* stateTopic, const char* valueTemplate,
                                   const char* deviceClass, const char* entityCategory,
                                   const char* icon)
{
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;

    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("binary_sensor", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc, availabilityField, sizeof(availabilityField));
    char deviceClassField[64] = {0};
    if (deviceClass && deviceClass[0] != '\0') {
        snprintf(deviceClassField, sizeof(deviceClassField), ",\"device_class\":\"%s\"", deviceClass);
    }
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }
    char iconField[64] = {0};
    if (icon && icon[0] != '\0') {
        snprintf(iconField, sizeof(iconField), ",\"icon\":\"%s\"", icon);
    }

    if (!formatChecked(payloadBuf, sizeof(payloadBuf),
             "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
             "\"state_topic\":\"%s\",\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
             "\"has_entity_name\":false%s%s%s%s,"
             "\"origin\":{\"name\":\"Flow.IO\"},"
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
             "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
             name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
             deviceClassField, entityCategoryField, iconField, availabilityField,
             deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
        LOGW("HA binary_sensor payload truncated object=%s", objectId);
        return false;
    }

    return publishDiscovery("binary_sensor", objectId, payloadBuf);
}

bool HAModule::publishSwitch(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic,
                             const char* payloadOn, const char* payloadOff,
                             const char* icon,
                             const char* entityCategory)
{
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !payloadOn || !payloadOff) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("switch", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc, availabilityField, sizeof(availabilityField));
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"state_on\":\"ON\",\"state_off\":\"OFF\","
                 "\"command_topic\":\"%s\",\"payload_on\":\"%s\",\"payload_off\":\"%s\","
                 "\"icon\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff, icon,
                 entityCategoryField, availabilityField,
                 deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
            LOGW("HA switch payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"state_on\":\"ON\",\"state_off\":\"OFF\","
                 "\"command_topic\":\"%s\",\"payload_on\":\"%s\",\"payload_off\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff, entityCategoryField, availabilityField,
                 deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
            LOGW("HA switch payload truncated object=%s", objectId);
            return false;
        }
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
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("number", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc, availabilityField, sizeof(availabilityField));

    char unitField[48] = {0};
    if (unit && unit[0] != '\0') {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_measurement\":\"%s\"", unit);
    }
    char entityCategoryField[48] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"command_topic\":\"%s\",\"command_template\":\"%s\","
                 "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"mode\":\"%s\",\"icon\":\"%s\"%s%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 (double)minValue, (double)maxValue, (double)step, mode ? mode : "slider", icon, entityCategoryField, unitField, availabilityField,
                 deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
            LOGW("HA number payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"command_topic\":\"%s\",\"command_template\":\"%s\","
                 "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"mode\":\"%s\"%s%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 (double)minValue, (double)maxValue, (double)step, mode ? mode : "slider", entityCategoryField, unitField, availabilityField,
                 deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
            LOGW("HA number payload truncated object=%s", objectId);
            return false;
        }
    }
    return publishDiscovery("number", objectId, payloadBuf);
}

bool HAModule::publishButton(const char* objectId, const char* name,
                             const char* commandTopic, const char* payloadPress,
                             const char* entityCategory, const char* icon)
{
    if (!objectId || !name || !commandTopic || !payloadPress) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("button", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc, availabilityField, sizeof(availabilityField));
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
                 "\"command_topic\":\"%s\",\"payload_press\":\"%s\",\"icon\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 commandTopic, payloadPress, icon,
                 entityCategoryField, availabilityField,
                 deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
            LOGW("HA button payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(payloadBuf, sizeof(payloadBuf),
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
                 "\"command_topic\":\"%s\",\"payload_press\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 commandTopic, payloadPress,
                 entityCategoryField, availabilityField,
                 deviceIdent, cfgData.vendor, cfgData.vendor, cfgData.model, FIRMW)) {
            LOGW("HA button payload truncated object=%s", objectId);
            return false;
        }
    }

    return publishDiscovery("button", objectId, payloadBuf);
}

bool HAModule::publishAutoconfig()
{
    return publishRegisteredEntities();
}

void HAModule::setStartupReady(bool ready)
{
    startupReady_ = ready;
    if (ready) {
        signalAutoconfigCheck();
    }
}

bool HAModule::publishRegisteredEntities()
{
    if (!mqttSvc || !mqttSvc->formatTopic) return false;

    bool okAll = true;
    const TickType_t stepDelay = pdMS_TO_TICKS(Limits::Ha::Timing::DiscoveryStepMs);

    for (uint8_t i = 0; i < sensorCount_; ++i) {
        const HASensorEntry& e = sensors_[i];
        if (!buildObjectId(e.objectSuffix, objectIdBuf, sizeof(objectIdBuf))) {
            okAll = false;
            continue;
        }
        mqttSvc->formatTopic(mqttSvc->ctx, e.stateTopicSuffix, stateTopicBuf, sizeof(stateTopicBuf));
        if (!publishSensor(objectIdBuf, e.name, stateTopicBuf, e.valueTemplate,
                           e.entityCategory, e.icon, e.unit, e.hasEntityName, e.availabilityTemplate)) {
            okAll = false;
        }
        if (stepDelay > 0) vTaskDelay(stepDelay);
    }

    for (uint8_t i = 0; i < binarySensorCount_; ++i) {
        const HABinarySensorEntry& e = binarySensors_[i];
        if (!buildObjectId(e.objectSuffix, objectIdBuf, sizeof(objectIdBuf))) {
            okAll = false;
            continue;
        }
        mqttSvc->formatTopic(mqttSvc->ctx, e.stateTopicSuffix, stateTopicBuf, sizeof(stateTopicBuf));
        if (!publishBinarySensor(objectIdBuf, e.name, stateTopicBuf, e.valueTemplate, e.deviceClass, e.entityCategory, e.icon)) {
            okAll = false;
        }
        if (stepDelay > 0) vTaskDelay(stepDelay);
    }

    for (uint8_t i = 0; i < switchCount_; ++i) {
        const HASwitchEntry& e = switches_[i];
        if (!buildObjectId(e.objectSuffix, objectIdBuf, sizeof(objectIdBuf))) {
            okAll = false;
            continue;
        }
        mqttSvc->formatTopic(mqttSvc->ctx, e.stateTopicSuffix, stateTopicBuf, sizeof(stateTopicBuf));
        mqttSvc->formatTopic(mqttSvc->ctx, e.commandTopicSuffix, commandTopicBuf, sizeof(commandTopicBuf));
        if (!publishSwitch(objectIdBuf, e.name, stateTopicBuf, e.valueTemplate,
                           commandTopicBuf, e.payloadOn, e.payloadOff, e.icon, e.entityCategory)) {
            okAll = false;
        }
        if (stepDelay > 0) vTaskDelay(stepDelay);
    }

    for (uint8_t i = 0; i < numberCount_; ++i) {
        const HANumberEntry& e = numbers_[i];
        if (!buildObjectId(e.objectSuffix, objectIdBuf, sizeof(objectIdBuf))) {
            okAll = false;
            continue;
        }
        mqttSvc->formatTopic(mqttSvc->ctx, e.stateTopicSuffix, stateTopicBuf, sizeof(stateTopicBuf));
        mqttSvc->formatTopic(mqttSvc->ctx, e.commandTopicSuffix, commandTopicBuf, sizeof(commandTopicBuf));
        if (!publishNumber(objectIdBuf, e.name, stateTopicBuf, e.valueTemplate,
                           commandTopicBuf, e.commandTemplate,
                           e.minValue, e.maxValue, e.step,
                           e.mode, e.entityCategory, e.icon, e.unit)) {
            okAll = false;
        }
        if (stepDelay > 0) vTaskDelay(stepDelay);
    }

    for (uint8_t i = 0; i < buttonCount_; ++i) {
        const HAButtonEntry& e = buttons_[i];
        if (!buildObjectId(e.objectSuffix, objectIdBuf, sizeof(objectIdBuf))) {
            okAll = false;
            continue;
        }
        mqttSvc->formatTopic(mqttSvc->ctx, e.commandTopicSuffix, commandTopicBuf, sizeof(commandTopicBuf));
        if (!publishButton(objectIdBuf, e.name, commandTopicBuf, e.payloadPress, e.entityCategory, e.icon)) {
            okAll = false;
        }
        if (stepDelay > 0) vTaskDelay(stepDelay);
    }

    return okAll;
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
    entityHash3_ = hash3Digits(deviceId);
}

void HAModule::tryPublishAutoconfig()
{
    if (published && !refreshRequested) return;
    if (!startupReady_) return;
    refreshIdentityFromConfig();
    if (!cfgData.enabled) return;
    if (!mqttSvc || !mqttSvc->isConnected || !dsSvc || !dsSvc->store) return;
    if (!mqttSvc->isConnected(mqttSvc->ctx)) return;
    if (!mqttReady(*dsSvc->store)) return;

    setHaVendor(*dsSvc->store, cfgData.vendor);
    setHaDeviceId(*dsSvc->store, deviceId);

    if (publishAutoconfig()) {
        published = true;
        refreshRequested = false;
        retryAfterMs_ = 0;
        setHaAutoconfigPublished(*dsSvc->store, true);
        LOGI("Home Assistant auto-discovery published (sensor=%u switch=%u number=%u button=%u)",
             (unsigned)sensorCount_, (unsigned)switchCount_, (unsigned)numberCount_, (unsigned)buttonCount_);
    } else {
        setHaAutoconfigPublished(*dsSvc->store, false);
        retryAfterMs_ = millis() + RETRY_DELAY_MS;
        autoconfigPending = true;
        LOGW("Home Assistant auto-discovery publish failed (retry in %lu ms)",
             (unsigned long)RETRY_DELAY_MS);
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

void HAModule::requestAutoconfigRefresh()
{
    published = false;
    refreshRequested = true;
    if (dsSvc && dsSvc->store) {
        setHaAutoconfigPublished(*dsSvc->store, false);
    }
    signalAutoconfigCheck();
}

void HAModule::loop()
{
    if (!autoconfigPending) {
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
    }
    if (!autoconfigPending) return;
    const uint32_t now = millis();
    if (retryAfterMs_ != 0 && (int32_t)(now - retryAfterMs_) < 0) {
        return;
    }
    autoconfigPending = false;
    tryPublishAutoconfig();
}

void HAModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Ha;
    constexpr uint16_t kCfgBranchId = (uint16_t)ConfigBranchId::Ha;
    cfg.registerVar(enabledVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(vendorVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(deviceIdVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(prefixVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(modelVar, kCfgModuleId, kCfgBranchId);

    eventBusSvc = services.get<EventBusService>("eventbus");
    dsSvc = services.get<DataStoreService>("datastore");
    mqttSvc = services.get<MqttService>("mqtt");

    haSvc.addSensor = HAModule::svcAddSensor;
    haSvc.addBinarySensor = HAModule::svcAddBinarySensor;
    haSvc.addSwitch = HAModule::svcAddSwitch;
    haSvc.addNumber = HAModule::svcAddNumber;
    haSvc.addButton = HAModule::svcAddButton;
    haSvc.requestRefresh = HAModule::svcRequestRefresh;
    haSvc.ctx = this;
    services.add("ha", &haSvc);

    const HASensorEntry alarmsPack{
        "alarms",
        "alarms_pack",
        "Alarms Pack",
        "rt/alarms/p",
        "{{ value_json.p | int(0) }}",
        "diagnostic",
        "mdi:alarm-light-outline",
        nullptr
    };
    (void)addSensorEntry(alarmsPack);

    const HASensorEntry uptimeSeconds{
        "system",
        "uptime_seconds",
        "Uptime",
        "rt/system/state",
        "{{ value_json.upt_s | int(0) }}",
        "diagnostic",
        "mdi:timer-outline",
        "s"
    };
    (void)addSensorEntry(uptimeSeconds);

    const HASensorEntry heapFreeBytes{
        "system",
        "heap_free_bytes",
        "Heap Free",
        "rt/system/state",
        "{{ ((value_json.heap.free | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko"
    };
    (void)addSensorEntry(heapFreeBytes);

    const HASensorEntry heapMinFreeBytes{
        "system",
        "heap_min_free_bytes",
        "Heap Min Free",
        "rt/system/state",
        "{{ ((value_json.heap.min | float(0)) / 1024) | round(1) }}",
        "diagnostic",
        "mdi:memory",
        "ko"
    };
    (void)addSensorEntry(heapMinFreeBytes);

    const HASensorEntry heapFragPercent{
        "system",
        "heap_fragmentation",
        "Heap Fragmentation",
        "rt/system/state",
        "{{ value_json.heap.frag | int(0) }}",
        "diagnostic",
        "mdi:chart-donut",
        "%"
    };
    (void)addSensorEntry(heapFragPercent);

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
