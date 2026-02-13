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

bool HAModule::buildObjectId(const char* suffix, char* out, size_t outLen) const
{
    if (!suffix || !out || outLen == 0) return false;
    char raw[256] = {0};
    snprintf(raw, sizeof(raw), "flowio%03u_%s", (unsigned)entityHash3_, suffix);
    sanitizeId(raw, out, outLen);
    return out[0] != '\0';
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
    char defaultEntityId[224] = {0};
    snprintf(defaultEntityId, sizeof(defaultEntityId), "sensor.%s", objectId);

    char availabilityField[320] = {0};
    if (mqttSvc && mqttSvc->formatTopic) {
        char availabilityTopic[192] = {0};
        mqttSvc->formatTopic(mqttSvc->ctx, "status", availabilityTopic, sizeof(availabilityTopic));
        if (availabilityTopic[0] != '\0') {
            snprintf(
                availabilityField,
                sizeof(availabilityField),
                ",\"availability\":[{\"topic\":\"%s\",\"value_template\":\"{{ value_json.online }}\"}],"
                "\"availability_mode\":\"all\",\"payload_available\":\"true\",\"payload_not_available\":\"false\"",
                availabilityTopic
            );
        }
    }

    snprintf(payloadBuf, sizeof(payloadBuf),
             "{\"name\":\"%s\",\"object_id\":\"%s\",\"default_entity_id\":\"%s\",\"unique_id\":\"%s\","
             "\"state_topic\":\"%s\",\"value_template\":\"%s\",\"state_class\":\"measurement\"%s%s%s%s,"
             "\"origin\":{\"name\":\"FlowIO\"},"
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"FlowIO\","
             "\"manufacturer\":\"%s\",\"model\":\"%s\"}}",
             name, objectId, defaultEntityId, objectId,
             stateTopic, valueTemplate,
             entityCategoryField, iconField, unitField, availabilityField,
             deviceIdent, cfgData.vendor, cfgData.model);

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

bool HAModule::publishAutoconfig()
{
    return publishRegisteredEntities();
}

bool HAModule::publishRegisteredEntities()
{
    if (!mqttSvc || !mqttSvc->formatTopic) return false;

    bool okAll = true;

    for (uint8_t i = 0; i < sensorCount_; ++i) {
        const HASensorEntry& e = sensors_[i];
        if (!buildObjectId(e.objectSuffix, objectIdBuf, sizeof(objectIdBuf))) {
            okAll = false;
            continue;
        }
        mqttSvc->formatTopic(mqttSvc->ctx, e.stateTopicSuffix, stateTopicBuf, sizeof(stateTopicBuf));
        if (!publishSensor(objectIdBuf, e.name, stateTopicBuf, e.valueTemplate, e.entityCategory, e.icon, e.unit)) {
            okAll = false;
        }
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
                           commandTopicBuf, e.payloadOn, e.payloadOff, e.icon)) {
            okAll = false;
        }
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
        setHaAutoconfigPublished(*dsSvc->store, true);
        LOGI("Home Assistant auto-discovery published (sensor=%u switch=%u number=%u)",
             (unsigned)sensorCount_, (unsigned)switchCount_, (unsigned)numberCount_);
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
    dsSvc = services.get<DataStoreService>("datastore");
    mqttSvc = services.get<MqttService>("mqtt");

    haSvc.addSensor = HAModule::svcAddSensor;
    haSvc.addBinarySensor = HAModule::svcAddBinarySensor;
    haSvc.addSwitch = HAModule::svcAddSwitch;
    haSvc.addNumber = HAModule::svcAddNumber;
    haSvc.requestRefresh = HAModule::svcRequestRefresh;
    haSvc.ctx = this;
    services.add("ha", &haSvc);

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
