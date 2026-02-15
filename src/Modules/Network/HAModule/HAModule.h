#pragma once
/**
 * @file HAModule.h
 * @brief Home Assistant auto-discovery publisher.
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/EventBus/EventBus.h"
#include "Core/Services/Services.h"
#include "Core/Runtime.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Active module that publishes Home Assistant MQTT discovery topics without blocking EventBus callbacks.
 */
class HAModule : public Module {
public:
    const char* moduleId() const override { return "ha"; }
    const char* taskName() const override { return "ha"; }
    uint16_t taskStackSize() const override { return 4096; }
    void loop() override;

    uint8_t dependencyCount() const override { return 4; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "eventbus";
        if (i == 1) return "config";
        if (i == 2) return "datastore";
        if (i == 3) return "mqtt";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;

private:
    static constexpr uint8_t MAX_HA_SENSORS = 24;
    static constexpr uint8_t MAX_HA_BINARY_SENSORS = 8;
    static constexpr uint8_t MAX_HA_SWITCHES = 16;
    static constexpr uint8_t MAX_HA_NUMBERS = 16;
    static constexpr size_t TOPIC_BUF_SIZE = 256;
    static constexpr size_t PAYLOAD_BUF_SIZE = 1536;

    struct HAConfig {
        bool enabled = true;
        char vendor[32] = "Flow.IO";
        char deviceId[32] = "";
        char discoveryPrefix[32] = "homeassistant";
        char model[40] = "Flow Controller";
    };

    const EventBusService* eventBusSvc = nullptr;
    const DataStoreService* dsSvc = nullptr;
    const MqttService* mqttSvc = nullptr;

    HAConfig cfgData{};
    volatile bool autoconfigPending = false;
    volatile bool refreshRequested = false;
    bool published = false;
    char deviceId[32] = {0};
    char deviceIdent[96] = {0};
    char nodeTopicId[32] = {0};
    uint16_t entityHash3_ = 0;

    char topicBuf[TOPIC_BUF_SIZE] = {0};
    char payloadBuf[PAYLOAD_BUF_SIZE] = {0};
    char stateTopicBuf[192] = {0};
    char objectIdBuf[192] = {0};
    char commandTopicBuf[192] = {0};

    HASensorEntry sensors_[MAX_HA_SENSORS]{};
    uint8_t sensorCount_ = 0;
    HABinarySensorEntry binarySensors_[MAX_HA_BINARY_SENSORS]{};
    uint8_t binarySensorCount_ = 0;
    HASwitchEntry switches_[MAX_HA_SWITCHES]{};
    uint8_t switchCount_ = 0;
    HANumberEntry numbers_[MAX_HA_NUMBERS]{};
    uint8_t numberCount_ = 0;

    HAService haSvc{};

    ConfigVariable<bool,0> enabledVar {
        NVS_KEY(NvsKeys::Ha::Enabled),"enabled","ha",ConfigType::Bool,
        &cfgData.enabled,ConfigPersistence::Persistent,0
    };
    ConfigVariable<char,0> vendorVar {
        NVS_KEY(NvsKeys::Ha::Vendor),"vendor","ha",ConfigType::CharArray,
        (char*)cfgData.vendor,ConfigPersistence::Persistent,sizeof(cfgData.vendor)
    };
    ConfigVariable<char,0> deviceIdVar {
        NVS_KEY(NvsKeys::Ha::DeviceId),"device_id","ha",ConfigType::CharArray,
        (char*)cfgData.deviceId,ConfigPersistence::Persistent,sizeof(cfgData.deviceId)
    };
    ConfigVariable<char,0> prefixVar {
        NVS_KEY(NvsKeys::Ha::DiscoveryPrefix),"discovery_prefix","ha",ConfigType::CharArray,
        (char*)cfgData.discoveryPrefix,ConfigPersistence::Persistent,sizeof(cfgData.discoveryPrefix)
    };
    ConfigVariable<char,0> modelVar {
        NVS_KEY(NvsKeys::Ha::Model),"model","ha",ConfigType::CharArray,
        (char*)cfgData.model,ConfigPersistence::Persistent,sizeof(cfgData.model)
    };

    static void onEventStatic(const Event& e, void* user);
    void onEvent(const Event& e);
    void signalAutoconfigCheck();
    void requestAutoconfigRefresh();
    void refreshIdentityFromConfig();
    void tryPublishAutoconfig();
    bool publishAutoconfig();
    bool publishRegisteredEntities();
    bool addSensorEntry(const HASensorEntry& entry);
    bool addBinarySensorEntry(const HABinarySensorEntry& entry);
    bool addSwitchEntry(const HASwitchEntry& entry);
    bool addNumberEntry(const HANumberEntry& entry);
    bool buildObjectId(const char* suffix, char* out, size_t outLen) const;
    bool buildDefaultEntityId(const char* component, const char* objectId, char* out, size_t outLen) const;
    bool buildUniqueId(const char* objectId, const char* name, char* out, size_t outLen) const;

    bool publishSensor(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       const char* unit = nullptr);
    bool publishBinarySensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* deviceClass = nullptr,
                             const char* entityCategory = nullptr,
                             const char* icon = nullptr);
    bool publishSwitch(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic,
                       const char* payloadOn, const char* payloadOff,
                       const char* icon = nullptr);
    bool publishNumber(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic, const char* commandTemplate,
                       float minValue, float maxValue, float step,
                       const char* mode = "slider",
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       const char* unit = nullptr);
    bool publishDiscovery(const char* component, const char* objectId, const char* payload);

    static void makeDeviceId(char* out, size_t len);
    static void makeHexNodeId(char* out, size_t len);
    static void sanitizeId(const char* in, char* out, size_t outLen);
    static uint16_t hash3Digits(const char* in);

    static bool svcAddSensor(void* ctx, const HASensorEntry* entry);
    static bool svcAddBinarySensor(void* ctx, const HABinarySensorEntry* entry);
    static bool svcAddSwitch(void* ctx, const HASwitchEntry* entry);
    static bool svcAddNumber(void* ctx, const HANumberEntry* entry);
    static bool svcRequestRefresh(void* ctx);
};
