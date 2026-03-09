#pragma once
/**
 * @file MQTTModule.h
 * @brief Unified MQTT module with job-based TX core.
 */

#include "Core/Module.h"
#include "Core/NvsKeys.h"
#include "Core/ConfigTypes.h"
#include "Core/ErrorCodes.h"
#include "Core/SystemLimits.h"
#include "Core/WokwiDefaultOverrides.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Modules/Network/MQTTModule/MqttConfigRouteProducer.h"
#include "Core/Services/Services.h"
#include "Modules/Network/MQTTModule/RuntimeProducer.h"
#include <mqtt_client.h>

/** @brief MQTT configuration values. */
struct MQTTConfig {
    bool enabled = FLOW_WIRDEF_MQ_EN;
    char host[Limits::Mqtt::Buffers::Host] = FLOW_WIRDEF_MQ_HOST;
    int32_t port = FLOW_WIRDEF_MQ_PORT;
    char user[Limits::Mqtt::Buffers::User] = FLOW_WIRDEF_MQ_USER;
    char pass[Limits::Mqtt::Buffers::Pass] = FLOW_WIRDEF_MQ_PASS;
    char baseTopic[Limits::Mqtt::Buffers::BaseTopic] = FLOW_WIRDEF_MQ_BASE;
    char topicDeviceId[Limits::Mqtt::Buffers::DeviceId] = FLOW_WIRDEF_MQ_TID;
};

/** @brief MQTT connection state. */
enum class MQTTState : uint8_t { Disabled, WaitingNetwork, Connecting, Connected, ErrorWait };

class MQTTModule : public Module {
public:
    const char* moduleId() const override { return "mqtt"; }
    const char* taskName() const override { return "mqtt"; }
    BaseType_t taskCore() const override { return 0; }

    uint8_t dependencyCount() const override { return 5; }
    const char* dependency(uint8_t i) const override {
        if (i == 0) return "loghub";
        if (i == 1) return "wifi";
        if (i == 2) return "cmd";
        if (i == 3) return "time";
        if (i == 4) return "alarms";
        return nullptr;
    }

    void init(ConfigStore& cfg, ServiceRegistry& services) override;
    void onConfigLoaded(ConfigStore& cfg, ServiceRegistry& services) override;
    void loop() override;
    uint16_t taskStackSize() const override { return Limits::Mqtt::TaskStackSize; }

    bool addRuntimePublisher(const char* topic,
                             uint32_t periodMs,
                             int qos,
                             bool retain,
                             bool (*build)(MQTTModule* self, char* out, size_t outLen),
                             bool allowNoPayload = false);

    bool registerRuntimeProvider(const IRuntimeSnapshotProvider* provider);
    bool enqueue(uint8_t producerId, uint16_t messageId, MqttPublishPriority priority, uint8_t flags = 0);
    bool registerProducer(const MqttPublishProducer* producer);

    void formatTopic(char* out, size_t outLen, const char* suffix) const;
    bool isConnected() const { return state_ == MQTTState::Connected; }
    void setStartupReady(bool ready) { startupReady_ = ready; }
    DataStore* dataStorePtr() const { return dataStore_; }

private:
    struct RxMsg {
        char topic[Limits::Mqtt::Buffers::RxTopic];
        char payload[Limits::Mqtt::Buffers::RxPayload];
    };

    struct RuntimePublisher {
        bool used = false;
        uint16_t messageId = 0;
        const char* topic = nullptr;
        uint32_t periodMs = 0;
        uint32_t nextDueMs = 0;
        uint8_t qos = 0;
        bool retain = false;
        bool (*build)(MQTTModule* self, char* out, size_t outLen) = nullptr;
        bool allowNoPayload = false;
    };

    struct AckMessage {
        bool used = false;
        uint16_t messageId = 0;
        uint8_t qos = 0;
        bool retain = false;
        char topicSuffix[16] = {0};
        char payload[Limits::Mqtt::Buffers::Reply] = {0};
    };

    struct Job {
        bool used = false;
        bool queued = false;
        bool processing = false;
        bool requeueAfterProcess = false;
        uint8_t producerId = 0;
        uint16_t messageId = 0;
        uint8_t priority = 0;
        uint8_t flags = 0;
        uint8_t retryCount = 0;
        uint32_t notBeforeMs = 0;
        uint8_t queuedPrio = 0;
        uint16_t queueToken = 0;
    };

    struct JobQueueItem {
        uint8_t slot = 0;
        uint16_t token = 0;
    };

    template <size_t N>
    struct JobRing {
        JobQueueItem items[N]{};
        uint16_t head = 0;
        uint16_t tail = 0;
        uint16_t count = 0;
    };

    static constexpr uint8_t ProducerIdAck = 1;
    static constexpr uint8_t ProducerIdStatus = 2;
    static constexpr uint8_t ProducerIdConfig = 41;
    static constexpr uint8_t ProducerIdRuntime = RuntimeProducer::ProducerId;
    static constexpr uint8_t ProducerIdAlarm = 5;

    static constexpr uint16_t StatusMsgOnline = 1;
    static constexpr uint16_t StatusMsgRuntimeBase = 32;

    static constexpr uint16_t AlarmMsgMeta = 1;
    static constexpr uint16_t AlarmMsgPack = 2;
    static constexpr uint16_t AlarmMsgStateBase = 100;

    static constexpr uint8_t MaxProducers = 24;
    static constexpr uint8_t MaxAckMessages = 2;
    static constexpr uint8_t MaxJobs = 80;
    static constexpr uint16_t RetryMinMs = 250;
    static constexpr uint16_t RetryMaxMs = 10000;
    static constexpr uint8_t ProcessBudgetPerTick = 8;

    static constexpr uint16_t HighQueueCap = 80;
    static constexpr uint16_t NormalQueueCap = 80;
    static constexpr uint16_t LowQueueCap = 60;

    MQTTConfig cfgData_{};
    // CFGDOC: {"label":"Hôte MQTT","help":"Adresse du broker MQTT (DNS ou IP)."}
    ConfigVariable<char,0> hostVar_{
        NVS_KEY(NvsKeys::Mqtt::Host), "host", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.host, ConfigPersistence::Persistent, sizeof(cfgData_.host)
    };
    // CFGDOC: {"label":"Port MQTT","help":"Port TCP utilisé pour la connexion au broker."}
    ConfigVariable<int32_t,0> portVar_{
        NVS_KEY(NvsKeys::Mqtt::Port), "port", "mqtt", ConfigType::Int32,
        &cfgData_.port, ConfigPersistence::Persistent, 0
    };
    // CFGDOC: {"label":"Utilisateur MQTT","help":"Nom d'utilisateur pour l'authentification MQTT."}
    ConfigVariable<char,0> userVar_{
        NVS_KEY(NvsKeys::Mqtt::User), "user", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.user, ConfigPersistence::Persistent, sizeof(cfgData_.user)
    };
    // CFGDOC: {"label":"Mot de passe MQTT","help":"Mot de passe pour l'authentification MQTT."}
    ConfigVariable<char,0> passVar_{
        NVS_KEY(NvsKeys::Mqtt::Pass), "pass", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.pass, ConfigPersistence::Persistent, sizeof(cfgData_.pass)
    };
    // CFGDOC: {"label":"Topic de base","help":"Préfixe MQTT pour les topics de télémétrie/commande."}
    ConfigVariable<char,0> baseTopicVar_{
        NVS_KEY(NvsKeys::Mqtt::BaseTopic), "baseTopic", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.baseTopic, ConfigPersistence::Persistent, sizeof(cfgData_.baseTopic)
    };
    // CFGDOC: {"label":"ID device MQTT topic","help":"Segment <deviceId> des topics MQTT. Vide = auto (MAC)."}
    ConfigVariable<char,0> topicDeviceIdVar_{
        NVS_KEY(NvsKeys::Mqtt::TopicDeviceId), "topicDeviceId", "mqtt", ConfigType::CharArray,
        (char*)cfgData_.topicDeviceId, ConfigPersistence::Persistent, sizeof(cfgData_.topicDeviceId)
    };
    // CFGDOC: {"label":"MQTT actif","help":"Active ou désactive le client MQTT."}
    ConfigVariable<bool,0> enabledVar_{
        NVS_KEY(NvsKeys::Mqtt::Enabled), "enabled", "mqtt", ConfigType::Bool,
        &cfgData_.enabled, ConfigPersistence::Persistent, 0
    };

    MQTTState state_ = MQTTState::WaitingNetwork;
    uint32_t stateTs_ = 0;

    esp_mqtt_client_handle_t client_ = nullptr;
    bool clientStarted_ = false;
    bool clientConfigDirty_ = true;
    bool suppressDisconnectEvent_ = false;

    const WifiService* wifiSvc_ = nullptr;
    const CommandService* cmdSvc_ = nullptr;
    const ConfigStoreService* cfgSvc_ = nullptr;
    const TimeSchedulerService* timeSchedSvc_ = nullptr;
    const AlarmService* alarmSvc_ = nullptr;
    const LogHubService* logHub_ = nullptr;
    EventBus* eventBus_ = nullptr;
    DataStore* dataStore_ = nullptr;

    char deviceId_[Limits::Mqtt::Buffers::DeviceId] = {0};
    char topicCmd_[Limits::Mqtt::Buffers::Topic] = {0};
    char topicStatus_[Limits::Mqtt::Buffers::Topic] = {0};
    char topicCfgSet_[Limits::Mqtt::Buffers::Topic] = {0};
    char brokerUri_[Limits::Mqtt::Buffers::DynamicTopic] = {0};

    QueueHandle_t rxQ_ = nullptr;
    StaticQueue_t rxQueueStruct_{};
    uint8_t* rxQueueStorage_ = nullptr;

    MqttService mqttSvc_{};

    const MqttPublishProducer* producers_[MaxProducers]{};
    uint8_t producerCount_ = 0;
    portMUX_TYPE producerMux_ = portMUX_INITIALIZER_UNLOCKED;

    Job jobs_[MaxJobs]{};
    JobRing<HighQueueCap> highQ_{};
    JobRing<NormalQueueCap> normalQ_{};
    JobRing<LowQueueCap> lowQ_{};
    portMUX_TYPE jobsMux_ = portMUX_INITIALIZER_UNLOCKED;

    char topicBuf_[Limits::Mqtt::Buffers::DynamicTopic] = {0};
    char payloadBuf_[Limits::Mqtt::Buffers::Publish] = {0};
    char replyBuf_[Limits::Mqtt::Buffers::Reply] = {0};

    AckMessage ackMessages_[MaxAckMessages]{};
    uint8_t ackWriteCursor_ = 0;
    uint16_t ackNextMessageId_ = 1;

    RuntimePublisher runtimePublishers_[Limits::Mqtt::Capacity::MaxPublishers]{};
    uint8_t runtimePublisherCount_ = 0;
    MqttConfigRouteProducer* cfgProducer_ = nullptr;
    RuntimeProducer runtimeProducerCore_{};

    bool netReady_ = false;
    uint32_t netReadyTs_ = 0;
    uint8_t retryCount_ = 0;
    uint32_t retryDelayMs_ = Limits::Mqtt::Backoff::MinMs;
    bool startupReady_ = false;

    uint32_t rxDropCount_ = 0;
    uint32_t parseFailCount_ = 0;
    uint32_t handlerFailCount_ = 0;
    uint32_t oversizeDropCount_ = 0;
    uint32_t lastEnqueueRejectLogMs_ = 0;
    uint32_t occLastReportMs_ = 0;
    uint16_t occMaxJobs_ = 0;
    uint16_t occMaxHigh_ = 0;
    uint16_t occMaxNormal_ = 0;
    uint16_t occMaxLow_ = 0;

    MqttPublishProducer ackProducerDesc_{};
    MqttPublishProducer statusProducerDesc_{};
    MqttPublishProducer runtimeProducerDesc_{};
    MqttPublishProducer alarmProducerDesc_{};

    void setState_(MQTTState s);
    void refreshTopicDeviceId_();
    void buildTopics_();
    void enqueueAlarmFullSync_();

    void connectMqtt_();
    bool ensureClient_();
    void stopClient_(bool intentional);
    void destroyClient_();

    void onConnect_(bool sessionPresent);
    void onDisconnect_(const esp_mqtt_error_codes_t* err);
    void onMessage_(const char* topic,
                    size_t topicLen,
                    const char* payload,
                    size_t len,
                    size_t index,
                    size_t total);

    void processRx_(const RxMsg& msg);
    void processRxCmd_(const RxMsg& msg);
    void processRxCfgSet_(const RxMsg& msg);
    void publishRxError_(const char* ackTopicSuffix, ErrorCode code, const char* where, bool parseFailure);
    void updateAndReportQueueOccupancy_(uint32_t nowMs);
    void tickProducers_(uint32_t nowMs);

    bool enqueueAck_(const char* topicSuffix,
                     const char* payload,
                     uint8_t qos,
                     bool retain,
                     MqttPublishPriority priority);

    bool tryPublishNow_(const char* topic, const char* payload, uint8_t qos, bool retain);
    void processJobs_(uint32_t nowMs);

    bool enqueueJob_(uint8_t producerId, uint16_t messageId, uint8_t priority, uint8_t flags);
    void snapshotQueueStatsNoLock_(uint16_t& jobsUsed,
                                   uint16_t& highCount,
                                   uint16_t& normalCount,
                                   uint16_t& lowCount) const;
    void logEnqueueReject_(uint8_t producerId,
                           uint16_t messageId,
                           uint8_t priority,
                           const char* reason,
                           uint16_t jobsUsed,
                           uint16_t highCount,
                           uint16_t normalCount,
                           uint16_t lowCount);
    bool queuePush_(uint8_t prio, const JobQueueItem& item);
    bool queuePop_(uint8_t prio, JobQueueItem& out);
    bool queueSlot_(uint8_t slotIdx, uint8_t prio, bool invalidateOld);
    bool dequeueNextJob_(uint32_t nowMs, uint8_t& slotIdx);
    int16_t findJobSlot_(uint8_t producerId, uint16_t messageId) const;
    int16_t allocJobSlot_();

    const MqttPublishProducer* findProducer_(uint8_t producerId) const;

    static void mqttEventHandlerStatic_(void* handler_args,
                                        esp_event_base_t base,
                                        int32_t event_id,
                                        void* event_data);
    static void onEventStatic_(const Event& e, void* user);
    void onEvent_(const Event& e);

    void syncRxMetrics_();
    void countRxDrop_();
    void countOversizeDrop_();

    static bool svcEnqueue_(void* ctx, uint8_t producerId, uint16_t messageId, uint8_t prio, uint8_t flags);
    static bool svcRegisterProducer_(void* ctx, const MqttPublishProducer* producer);
    static void svcFormatTopic_(void* ctx, const char* suffix, char* out, size_t outLen);
    static bool svcIsConnected_(void* ctx);

    static MqttBuildResult buildAckStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static void onAckPublishedStatic_(void* ctx, uint16_t messageId);
    static void onAckDroppedStatic_(void* ctx, uint16_t messageId);

    static MqttBuildResult buildStatusStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);
    static MqttBuildResult buildAlarmStatic_(void* ctx, uint16_t messageId, MqttBuildContext& buildCtx);

    MqttBuildResult buildAck_(uint16_t messageId, MqttBuildContext& buildCtx);
    void onAckPublished_(uint16_t messageId);
    void onAckDropped_(uint16_t messageId);

    MqttBuildResult buildStatus_(uint16_t messageId, MqttBuildContext& buildCtx);
    MqttBuildResult buildAlarm_(uint16_t messageId, MqttBuildContext& buildCtx);
};
