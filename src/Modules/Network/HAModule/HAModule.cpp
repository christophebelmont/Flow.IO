/**
 * @file HAModule.cpp
 * @brief Implementation file.
 */

#include "HAModule.h"
#include "Modules/Network/HAModule/HARuntime.h"
#include "Core/MqttTopics.h"
#include "Core/SystemLimits.h"
#include <esp_system.h>
#include <ctype.h>
#include <new>
#include <stdarg.h>
#include <string.h>

#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::HAModule)
#include "Core/ModuleLog.h"

#ifndef FIRMW
#define FIRMW "unknown"
#endif

namespace {
static constexpr uint8_t kHaCfgBranch = 1;
static constexpr MqttConfigRouteProducer::Route kHaCfgRoutes[] = {
    {1, {(uint8_t)ConfigModuleId::Ha, kHaCfgBranch}, "ha", "ha", (uint8_t)MqttPublishPriority::Normal, nullptr},
};
}

static void buildAvailabilityField(const MqttService* mqttSvc_,
                                   char* out,
                                   size_t outLen,
                                   const char* stateTopic = nullptr,
                                   const char* stateAvailabilityTemplate = nullptr)
{
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return;

    char availabilityTopic[192] = {0};
    mqttSvc_->formatTopic(mqttSvc_->ctx, MqttTopics::SuffixStatus, availabilityTopic, sizeof(availabilityTopic));
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
        snprintf(out, outLen, "%s_%s_%s", deviceId_, objectId, cleanName);
    } else {
        snprintf(out, outLen, "%s_%s", deviceId_, objectId);
    }
    return out[0] != '\0';
}

bool HAModule::publishDiscovery(const char* component, const char* objectId, MqttBuildContext& outCtx)
{
    if (!component || !objectId) return false;
    if (!outCtx.topic || outCtx.topicCapacity == 0 || !outCtx.payload || outCtx.payloadCapacity == 0) return false;
    if (!formatChecked(outCtx.topic,
                       outCtx.topicCapacity,
                       "%s/%s/%s/%s/config",
                       cfgData_.discoveryPrefix,
                       component,
                       nodeTopicId_,
                       objectId)) {
        LOGW("HA discovery topic truncated component=%s object=%s", component, objectId);
        return false;
    }
    outCtx.topicLen = (uint16_t)strnlen(outCtx.topic, outCtx.topicCapacity);
    outCtx.payloadLen = (uint16_t)strnlen(outCtx.payload, outCtx.payloadCapacity);
    outCtx.qos = 0;
    outCtx.retain = true;
    return true;
}

bool HAModule::publishSensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* entityCategory, const char* icon, const char* unit,
                             bool hasEntityName,
                             const char* availabilityTemplate,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
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
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField), stateTopic, availabilityTemplate);

    if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
             "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
             "\"state_topic\":\"%s\",\"value_template\":\"%s\",\"state_class\":\"measurement\"%s%s%s%s%s,"
             "\"origin\":{\"name\":\"Flow.IO\"},"
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
             "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
             name, objectId, defaultEntityId, uniqueId,
             stateTopic, valueTemplate,
             entityCategoryField, iconField, unitField, hasEntityNameField, availabilityField,
             deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
        LOGW("HA sensor payload truncated object=%s", objectId);
        return false;
    }

    return publishDiscovery("sensor", objectId, buildCtx);
}

bool HAModule::publishBinarySensor(const char* objectId, const char* name,
                                   const char* stateTopic, const char* valueTemplate,
                                   const char* deviceClass, const char* entityCategory,
                                   const char* icon,
                                   MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate) return false;

    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("binary_sensor", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
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

    if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
             "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
             "\"state_topic\":\"%s\",\"value_template\":\"%s\",\"payload_on\":\"True\",\"payload_off\":\"False\","
             "\"has_entity_name\":false%s%s%s%s,"
             "\"origin\":{\"name\":\"Flow.IO\"},"
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
             "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
             name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
             deviceClassField, entityCategoryField, iconField, availabilityField,
             deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
        LOGW("HA binary_sensor payload truncated object=%s", objectId);
        return false;
    }

    return publishDiscovery("binary_sensor", objectId, buildCtx);
}

bool HAModule::publishSwitch(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic,
                             const char* payloadOn, const char* payloadOff,
                             const char* icon,
                             const char* entityCategory,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !payloadOn || !payloadOff) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("switch", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
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
                 deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
            LOGW("HA switch payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"state_on\":\"ON\",\"state_off\":\"OFF\","
                 "\"command_topic\":\"%s\",\"payload_on\":\"%s\",\"payload_off\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate,
                 commandTopic, payloadOn, payloadOff, entityCategoryField, availabilityField,
                 deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
            LOGW("HA switch payload truncated object=%s", objectId);
            return false;
        }
    }
    return publishDiscovery("switch", objectId, buildCtx);
}

bool HAModule::publishNumber(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* commandTopic, const char* commandTemplate,
                             float minValue, float maxValue, float step,
                             const char* mode, const char* entityCategory, const char* icon, const char* unit,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !stateTopic || !valueTemplate || !commandTopic || !commandTemplate) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("number", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));

    char unitField[48] = {0};
    if (unit && unit[0] != '\0') {
        snprintf(unitField, sizeof(unitField), ",\"unit_of_measurement\":\"%s\"", unit);
    }
    char entityCategoryField[48] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"command_topic\":\"%s\",\"command_template\":\"%s\","
                 "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"mode\":\"%s\",\"icon\":\"%s\"%s%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 (double)minValue, (double)maxValue, (double)step, mode ? mode : "slider", icon, entityCategoryField, unitField, availabilityField,
                 deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
            LOGW("HA number payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\",\"state_topic\":\"%s\","
                 "\"value_template\":\"%s\",\"command_topic\":\"%s\",\"command_template\":\"%s\","
                 "\"min\":%.3f,\"max\":%.3f,\"step\":%.3f,\"mode\":\"%s\"%s%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId, stateTopic, valueTemplate, commandTopic, commandTemplate,
                 (double)minValue, (double)maxValue, (double)step, mode ? mode : "slider", entityCategoryField, unitField, availabilityField,
                 deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
            LOGW("HA number payload truncated object=%s", objectId);
            return false;
        }
    }
    return publishDiscovery("number", objectId, buildCtx);
}

bool HAModule::publishButton(const char* objectId, const char* name,
                             const char* commandTopic, const char* payloadPress,
                             const char* entityCategory, const char* icon,
                             MqttBuildContext* outCtx)
{
    if (!outCtx) return false;
    MqttBuildContext& buildCtx = *outCtx;
    if (!objectId || !name || !commandTopic || !payloadPress) return false;
    char defaultEntityId[224] = {0};
    if (!buildDefaultEntityId("button", objectId, defaultEntityId, sizeof(defaultEntityId))) return false;
    char uniqueId[256] = {0};
    if (!buildUniqueId(objectId, name, uniqueId, sizeof(uniqueId))) return false;
    char availabilityField[384] = {0};
    buildAvailabilityField(mqttSvc_, availabilityField, sizeof(availabilityField));
    char entityCategoryField[64] = {0};
    if (entityCategory && entityCategory[0] != '\0') {
        snprintf(entityCategoryField, sizeof(entityCategoryField), ",\"entity_category\":\"%s\"", entityCategory);
    }

    if (icon && icon[0] != '\0') {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
                 "\"command_topic\":\"%s\",\"payload_press\":\"%s\",\"icon\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 commandTopic, payloadPress, icon,
                 entityCategoryField, availabilityField,
                 deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
            LOGW("HA button payload truncated object=%s", objectId);
            return false;
        }
    } else {
        if (!formatChecked(buildCtx.payload, buildCtx.payloadCapacity,
                 "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
                 "\"command_topic\":\"%s\",\"payload_press\":\"%s\"%s%s,"
                 "\"origin\":{\"name\":\"Flow.IO\"},"
                 "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                 "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}}",
                 name, objectId, defaultEntityId, uniqueId,
                 commandTopic, payloadPress,
                 entityCategoryField, availabilityField,
                 deviceIdent_, cfgData_.vendor, cfgData_.vendor, cfgData_.model, FIRMW)) {
            LOGW("HA button payload truncated object=%s", objectId);
            return false;
        }
    }

    return publishDiscovery("button", objectId, buildCtx);
}

void HAModule::setStartupReady(bool ready)
{
    startupReady_ = ready;
    if (ready) {
        (void)enqueuePending_(MqttPublishPriority::Low);
    }
}

void HAModule::refreshIdentityFromConfig()
{
    if (cfgData_.deviceId[0] != '\0') {
        strncpy(deviceId_, cfgData_.deviceId, sizeof(deviceId_) - 1);
        deviceId_[sizeof(deviceId_) - 1] = '\0';
    } else {
        makeHexNodeId(deviceId_, sizeof(deviceId_));
    }
    sanitizeId(deviceId_, nodeTopicId_, sizeof(nodeTopicId_));
    if (nodeTopicId_[0] == '\0') {
        snprintf(nodeTopicId_, sizeof(nodeTopicId_), "flowio");
    }
    snprintf(deviceIdent_, sizeof(deviceIdent_), "%s-%s", cfgData_.vendor, deviceId_);
    entityHash3_ = hash3Digits(deviceId_);
}

uint16_t HAModule::entityCount_() const
{
    return (uint16_t)sensorCount_ +
           (uint16_t)binarySensorCount_ +
           (uint16_t)switchCount_ +
           (uint16_t)numberCount_ +
           (uint16_t)buttonCount_;
}

bool HAModule::isPending_(uint16_t messageId) const
{
    if (messageId >= MAX_HA_ENTITIES) return false;
    const uint16_t word = (uint16_t)(messageId / 32U);
    const uint16_t bit = (uint16_t)(messageId % 32U);
    return (pendingBits_[word] & (1UL << bit)) != 0UL;
}

void HAModule::setPending_(uint16_t messageId, bool pending)
{
    if (messageId >= MAX_HA_ENTITIES) return;
    const uint16_t word = (uint16_t)(messageId / 32U);
    const uint16_t bit = (uint16_t)(messageId % 32U);
    const uint32_t mask = (1UL << bit);
    if (pending) pendingBits_[word] |= mask;
    else pendingBits_[word] &= ~mask;
}

void HAModule::markAllPending_()
{
    memset(pendingBits_, 0, sizeof(pendingBits_));
    const uint16_t count = entityCount_();
    for (uint16_t i = 0; i < count; ++i) {
        setPending_(i, true);
    }
}

bool HAModule::anyPending_() const
{
    const uint16_t words = (uint16_t)(sizeof(pendingBits_) / sizeof(pendingBits_[0]));
    for (uint16_t i = 0; i < words; ++i) {
        if (pendingBits_[i] != 0U) return true;
    }
    return false;
}

bool HAModule::enqueuePending_(MqttPublishPriority prio)
{
    if (!mqttSvc_ || !mqttSvc_->enqueue) return false;
    if (!startupReady_) return false;

    bool any = false;
    const uint16_t count = entityCount_();
    for (uint16_t i = 0; i < count; ++i) {
        if (!isPending_(i)) continue;
        any = mqttSvc_->enqueue(mqttSvc_->ctx, ProducerId, i, (uint8_t)prio, 0) || any;
    }
    return any;
}

MqttBuildResult HAModule::producerBuildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    return self ? self->buildMessage_(messageId, buildCtx) : MqttBuildResult::PermanentError;
}

void HAModule::producerPublishedStatic_(void* ctx, uint16_t messageId)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (self) self->onMessagePublished_(messageId);
}

void HAModule::producerDroppedStatic_(void* ctx, uint16_t messageId)
{
    HAModule* self = static_cast<HAModule*>(ctx);
    if (self) self->onMessageDropped_(messageId);
}

bool HAModule::buildEntityMessage_(uint16_t messageId, MqttBuildContext& buildCtx)
{
    bool ok = false;
    uint16_t cursor = 0;

    if (messageId < (uint16_t)(cursor + sensorCount_)) {
        const HASensorEntry& e = sensors_[messageId - cursor];
        if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
            mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
            ok = publishSensor(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                               e.entityCategory, e.icon, e.unit, e.hasEntityName, e.availabilityTemplate, &buildCtx);
        }
    } else {
        cursor = (uint16_t)(cursor + sensorCount_);
        if (messageId < (uint16_t)(cursor + binarySensorCount_)) {
            const HABinarySensorEntry& e = binarySensors_[messageId - cursor];
            if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                ok = publishBinarySensor(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate, e.deviceClass, e.entityCategory, e.icon, &buildCtx);
            }
        } else {
            cursor = (uint16_t)(cursor + binarySensorCount_);
            if (messageId < (uint16_t)(cursor + switchCount_)) {
                const HASwitchEntry& e = switches_[messageId - cursor];
                if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                    mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                    mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                    ok = publishSwitch(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                                       commandTopicBuf_, e.payloadOn, e.payloadOff, e.icon, e.entityCategory, &buildCtx);
                }
            } else {
                cursor = (uint16_t)(cursor + switchCount_);
                if (messageId < (uint16_t)(cursor + numberCount_)) {
                    const HANumberEntry& e = numbers_[messageId - cursor];
                    if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                        mqttSvc_->formatTopic(mqttSvc_->ctx, e.stateTopicSuffix, stateTopicBuf_, sizeof(stateTopicBuf_));
                        mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                        ok = publishNumber(objectIdBuf_, e.name, stateTopicBuf_, e.valueTemplate,
                                           commandTopicBuf_, e.commandTemplate,
                                           e.minValue, e.maxValue, e.step,
                                           e.mode, e.entityCategory, e.icon, e.unit, &buildCtx);
                    }
                } else {
                    cursor = (uint16_t)(cursor + numberCount_);
                    if (messageId < (uint16_t)(cursor + buttonCount_)) {
                        const HAButtonEntry& e = buttons_[messageId - cursor];
                        if (buildObjectId(e.objectSuffix, objectIdBuf_, sizeof(objectIdBuf_))) {
                            mqttSvc_->formatTopic(mqttSvc_->ctx, e.commandTopicSuffix, commandTopicBuf_, sizeof(commandTopicBuf_));
                            ok = publishButton(objectIdBuf_, e.name, commandTopicBuf_, e.payloadPress, e.entityCategory, e.icon, &buildCtx);
                        }
                    }
                }
            }
        }
    }

    if (!ok) return false;
    return true;
}

MqttBuildResult HAModule::buildMessage_(uint16_t messageId, MqttBuildContext& buildCtx)
{
    if (!startupReady_) return MqttBuildResult::RetryLater;
    if (!cfgData_.enabled) return MqttBuildResult::NoLongerNeeded;
    if (!mqttSvc_ || !mqttSvc_->formatTopic) return MqttBuildResult::PermanentError;
    if (!dsSvc_ || !dsSvc_->store) return MqttBuildResult::RetryLater;
    if (!wifiReady(*dsSvc_->store)) return MqttBuildResult::RetryLater;
    if (!mqttReady(*dsSvc_->store)) return MqttBuildResult::RetryLater;
    if (!isPending_(messageId)) return MqttBuildResult::NoLongerNeeded;

    refreshIdentityFromConfig();
    setHaVendor(*dsSvc_->store, cfgData_.vendor);
    setHaDeviceId(*dsSvc_->store, deviceId_);

    if (!buildEntityMessage_(messageId, buildCtx)) return MqttBuildResult::PermanentError;
    return MqttBuildResult::Ready;
}

void HAModule::onMessagePublished_(uint16_t messageId)
{
    setPending_(messageId, false);
    if (dsSvc_ && dsSvc_->store) {
        const bool done = !anyPending_();
        setHaAutoconfigPublished(*dsSvc_->store, done);
    }
    published_ = !anyPending_();
}

void HAModule::onMessageDropped_(uint16_t messageId)
{
    setPending_(messageId, false);
    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, !anyPending_());
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
    if (!dsSvc_ || !dsSvc_->store) return;

    if (payload->id == DATAKEY_WIFI_READY) {
        if (wifiReady(*dsSvc_->store) && mqttReady(*dsSvc_->store)) {
            (void)enqueuePending_(MqttPublishPriority::Low);
        }
        return;
    }

    if (payload->id == DATAKEY_MQTT_READY) {
        if (wifiReady(*dsSvc_->store) && mqttReady(*dsSvc_->store)) {
            (void)enqueuePending_(MqttPublishPriority::Low);
        }
        return;
    }
}

void HAModule::requestAutoconfigRefresh()
{
    published_ = false;
    markAllPending_();
    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, false);
    }
    (void)enqueuePending_(MqttPublishPriority::Low);
}

void HAModule::loop()
{
    vTaskDelay(pdMS_TO_TICKS(250));
}

void HAModule::onConfigLoaded(ConfigStore&, ServiceRegistry& services)
{
    if (!cfgMqttPub_) {
        cfgMqttPub_ = new (std::nothrow) MqttConfigRouteProducer();
    }
    if (cfgMqttPub_) {
        cfgMqttPub_->configure(this,
                               ProducerIdCfg,
                               kHaCfgRoutes,
                               (uint8_t)(sizeof(kHaCfgRoutes) / sizeof(kHaCfgRoutes[0])),
                               services);
    }
}

void HAModule::init(ConfigStore& cfg, ServiceRegistry& services)
{
    constexpr uint8_t kCfgModuleId = (uint8_t)ConfigModuleId::Ha;
    constexpr uint8_t kCfgBranchId = kHaCfgBranch;
    cfg.registerVar(enabledVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(vendorVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(deviceIdVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(prefixVar, kCfgModuleId, kCfgBranchId);
    cfg.registerVar(modelVar, kCfgModuleId, kCfgBranchId);

    eventBusSvc_ = services.get<EventBusService>("eventbus");
    dsSvc_ = services.get<DataStoreService>("datastore");
    mqttSvc_ = services.get<MqttService>("mqtt");

    haSvc_.addSensor = HAModule::svcAddSensor;
    haSvc_.addBinarySensor = HAModule::svcAddBinarySensor;
    haSvc_.addSwitch = HAModule::svcAddSwitch;
    haSvc_.addNumber = HAModule::svcAddNumber;
    haSvc_.addButton = HAModule::svcAddButton;
    haSvc_.requestRefresh = HAModule::svcRequestRefresh;
    haSvc_.ctx = this;
    services.add("ha", &haSvc_);

    producer_.producerId = ProducerId;
    producer_.ctx = this;
    producer_.buildMessage = HAModule::producerBuildStatic_;
    producer_.onMessagePublished = HAModule::producerPublishedStatic_;
    producer_.onMessageDropped = HAModule::producerDroppedStatic_;
    if (mqttSvc_ && mqttSvc_->registerProducer) {
        producerRegistered_ = mqttSvc_->registerProducer(mqttSvc_->ctx, &producer_);
    }

    if (dsSvc_ && dsSvc_->store) {
        setHaAutoconfigPublished(*dsSvc_->store, false);
    }

    if (eventBusSvc_ && eventBusSvc_->bus) {
        eventBusSvc_->bus->subscribe(EventId::DataChanged, &HAModule::onEventStatic, this);
    }

    markAllPending_();
    if (dsSvc_ && dsSvc_->store && wifiReady(*dsSvc_->store) && mqttReady(*dsSvc_->store)) {
        (void)enqueuePending_(MqttPublishPriority::Low);
    }
}
