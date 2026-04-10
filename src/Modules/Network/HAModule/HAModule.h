#pragma once
/**
 * @file HAModule.h
 * @brief Home Assistant discovery producer.
 */

#include "Core/Module.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/NvsKeys.h"
#include "Core/EventBus/EventBus.h"
#include "Core/ServiceBinding.h"
#include "Core/Services/Services.h"
#include "Core/Runtime.h"
#include <stdint.h>
#include <stddef.h>

class HAModule : public Module {
public:
    ModuleId moduleId() const override { return ModuleId::Ha; }
    const char* taskName() const override { return "ha"; }
    BaseType_t taskCore() const override { return 0; }
    uint16_t taskStackSize() const override { return 3072; }
    uint8_t taskCount() const override { return 1; }
    const ModuleTaskSpec* taskSpecs() const override { return singleLoopTaskSpec(); }

    uint8_t dependencyCount() const override { return 4; }
    ModuleId dependency(uint8_t i) const override {
        if (i == 0) return ModuleId::EventBus;
        if (i == 1) return ModuleId::ConfigStore;
        if (i == 2) return ModuleId::DataStore;
        if (i == 3) return ModuleId::Mqtt;
        return ModuleId::Unknown;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore&, ServiceRegistry& services) override;
    void onStart(ConfigStore&, ServiceRegistry&) override { setStartupReady(true); }
    void loop() override;
    uint32_t startDelayMs() const override { return Limits::Boot::HaStartDelayMs; }
    void setStartupReady(bool ready);

private:
    static constexpr uint8_t ProducerId = 32;
    static constexpr uint8_t ProducerIdCfg = 49;

    static constexpr uint8_t MAX_HA_SENSORS = 40;
    static constexpr uint8_t MAX_HA_BINARY_SENSORS = 6;
    static constexpr uint8_t MAX_HA_SWITCHES = 14;
    static constexpr uint8_t MAX_HA_NUMBERS = 14;
    static constexpr uint8_t MAX_HA_BUTTONS = 12;
    static constexpr uint8_t MAX_HA_DISCOVERY_CLEANUPS = 9;
    static constexpr uint16_t MAX_HA_ENTITIES =
        MAX_HA_SENSORS + MAX_HA_BINARY_SENSORS + MAX_HA_SWITCHES + MAX_HA_NUMBERS + MAX_HA_BUTTONS;
    static constexpr uint16_t MAX_HA_MESSAGES = MAX_HA_ENTITIES + MAX_HA_DISCOVERY_CLEANUPS;

    struct HAConfig {
        bool enabled = true;
        char vendor[32] = "Flow.io";
        char deviceId[32] = "";
        char discoveryPrefix[32] = "homeassistant";
        char model[40] = "Flow Controller";
    };

    const EventBusService* eventBusSvc_ = nullptr;
    const DataStoreService* dsSvc_ = nullptr;
    const MqttService* mqttSvc_ = nullptr;

    HAConfig cfgData_{};
    bool startupReady_ = false;
    bool producerRegistered_ = false;
    bool published_ = false;

    char deviceId_[32] = {0};
    char deviceIdent_[64] = {0};
    char nodeTopicId_[32] = {0};
    uint16_t entityHash2_ = 0;

    char stateTopicBuf_[96] = {0};
    char objectIdBuf_[96] = {0};
    char commandTopicBuf_[96] = {0};

    HASensorEntry sensors_[MAX_HA_SENSORS]{};
    uint8_t sensorCount_ = 0;
    HABinarySensorEntry binarySensors_[MAX_HA_BINARY_SENSORS]{};
    uint8_t binarySensorCount_ = 0;
    HASwitchEntry switches_[MAX_HA_SWITCHES]{};
    uint8_t switchCount_ = 0;
    HANumberEntry numbers_[MAX_HA_NUMBERS]{};
    uint8_t numberCount_ = 0;
    HAButtonEntry buttons_[MAX_HA_BUTTONS]{};
    uint8_t buttonCount_ = 0;

    uint32_t pendingBits_[(MAX_HA_MESSAGES + 31U) / 32U] = {0};

    MqttPublishProducer producer_{};
    MqttConfigRouteProducer* cfgMqttPub_ = nullptr;

    // CFGDOC: {"label":"Auto-découverte HA active","help":"Active ou désactive la publication Home Assistant Discovery."}
    ConfigVariable<bool,0> enabledVar {
        NVS_KEY(NvsKeys::Ha::Enabled),"enabled","ha",ConfigType::Bool,
        &cfgData_.enabled,ConfigPersistence::Persistent,0
    };
    // CFGDOC: {"label":"Constructeur","help":"Nom du constructeur exposé dans Home Assistant."}
    ConfigVariable<char,0> vendorVar {
        NVS_KEY(NvsKeys::Ha::Vendor),"vendor","ha",ConfigType::CharArray,
        (char*)cfgData_.vendor,ConfigPersistence::Persistent,sizeof(cfgData_.vendor)
    };
    // CFGDOC: {"label":"Identifiant appareil","help":"Identifiant unique de l'appareil dans Home Assistant."}
    ConfigVariable<char,0> deviceIdVar {
        NVS_KEY(NvsKeys::Ha::DeviceId),"device_id","ha",ConfigType::CharArray,
        (char*)cfgData_.deviceId,ConfigPersistence::Persistent,sizeof(cfgData_.deviceId)
    };
    // CFGDOC: {"label":"Préfixe Discovery","help":"Préfixe MQTT utilisé pour les topics Home Assistant Discovery."}
    ConfigVariable<char,0> prefixVar {
        NVS_KEY(NvsKeys::Ha::DiscoveryPrefix),"disc_prefix","ha",ConfigType::CharArray,
        (char*)cfgData_.discoveryPrefix,ConfigPersistence::Persistent,sizeof(cfgData_.discoveryPrefix)
    };
    // CFGDOC: {"label":"Modèle","help":"Nom du modèle exposé dans Home Assistant."}
    ConfigVariable<char,0> modelVar {
        NVS_KEY(NvsKeys::Ha::Model),"model","ha",ConfigType::CharArray,
        (char*)cfgData_.model,ConfigPersistence::Persistent,sizeof(cfgData_.model)
    };

    static void onEventStatic(const Event& e, void* user);
    void onEvent(const Event& e);

    bool addSensorSvc_(const HASensorEntry* entry);
    bool addBinarySensorSvc_(const HABinarySensorEntry* entry);
    bool addSwitchSvc_(const HASwitchEntry* entry);
    bool addNumberSvc_(const HANumberEntry* entry);
    bool addButtonSvc_(const HAButtonEntry* entry);
    bool requestRefreshSvc_();
    bool addSensorEntry(const HASensorEntry& entry);
    bool addBinarySensorEntry(const HABinarySensorEntry& entry);
    bool addSwitchEntry(const HASwitchEntry& entry);
    bool addNumberEntry(const HANumberEntry& entry);
    bool addButtonEntry(const HAButtonEntry& entry);

    void requestAutoconfigRefresh();
    void refreshIdentityFromConfig();
    bool enqueuePending_(MqttPublishPriority prio);
    void markAllPending_();
    bool isPending_(uint16_t messageId) const;
    void setPending_(uint16_t messageId, bool pending);
    bool anyPending_() const;
    uint16_t entityCount_() const;
    uint16_t messageCount_() const;

    static MqttBuildResult producerBuildStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static void producerPublishedStatic_(void* ctx, uint16_t messageId);
    static void producerDroppedStatic_(void* ctx, uint16_t messageId);

    MqttBuildResult buildMessage_(uint16_t messageId, MqttBuildContext& buildCtx);
    void onMessagePublished_(uint16_t messageId);
    void onMessageDropped_(uint16_t messageId);

    bool buildEntityMessage_(uint16_t messageId, MqttBuildContext& buildCtx);
    bool buildLegacyCleanupMessage_(uint16_t cleanupId, MqttBuildContext& buildCtx);
    bool resolveMqttTopicDeviceId_(char* out, size_t outLen) const;

    bool buildObjectId(const char* suffix, char* out, size_t outLen) const;
    bool buildDefaultEntityId(const char* component, const char* objectId, char* out, size_t outLen) const;
    bool buildUniqueId(const char* objectId, const char* name, char* out, size_t outLen) const;

    bool publishSensor(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       const char* unit = nullptr,
                       bool hasEntityName = false,
                       const char* availabilityTemplate = nullptr,
                       bool isText = false,
                       MqttBuildContext* outCtx = nullptr);
    bool publishBinarySensor(const char* objectId, const char* name,
                             const char* stateTopic, const char* valueTemplate,
                             const char* deviceClass = nullptr,
                             const char* entityCategory = nullptr,
                             const char* icon = nullptr,
                             MqttBuildContext* outCtx = nullptr);
    bool publishSwitch(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic,
                       const char* payloadOn, const char* payloadOff,
                       const char* icon = nullptr,
                       const char* entityCategory = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishNumber(const char* objectId, const char* name,
                       const char* stateTopic, const char* valueTemplate,
                       const char* commandTopic, const char* commandTemplate,
                       float minValue, float maxValue, float step,
                       const char* mode = "slider",
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       const char* unit = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishButton(const char* objectId, const char* name,
                       const char* commandTopic, const char* payloadPress,
                       const char* entityCategory = nullptr,
                       const char* icon = nullptr,
                       MqttBuildContext* outCtx = nullptr);
    bool publishDiscovery(const char* component, const char* objectId, MqttBuildContext& outCtx);

    static void makeDeviceId(char* out, size_t len);
    static void makeHexNodeId(char* out, size_t len);
    static void sanitizeId(const char* in, char* out, size_t outLen);
    static uint16_t hash2Digits(const char* in);

    HAService haSvc_{
        ServiceBinding::bind<&HAModule::addSensorSvc_>,
        ServiceBinding::bind<&HAModule::addBinarySensorSvc_>,
        ServiceBinding::bind<&HAModule::addSwitchSvc_>,
        ServiceBinding::bind<&HAModule::addNumberSvc_>,
        ServiceBinding::bind<&HAModule::addButtonSvc_>,
        ServiceBinding::bind<&HAModule::requestRefreshSvc_>,
        this
    };
};
