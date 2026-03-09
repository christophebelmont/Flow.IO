#pragma once

#include "Core/SystemLimits.h"
#include "Core/RuntimeSnapshotProvider.h"
#include "Core/Services/Services.h"

class RuntimeProducer {
public:
    static constexpr uint8_t ProducerId = 4;

    void configure(const MqttService* mqttSvc);
    bool registerProvider(const IRuntimeSnapshotProvider* provider);
    void rebuildRoutes();

    void onConnected();
    void onDataChanged(DataKey key);

    MqttBuildResult buildMessage(uint16_t messageId, MqttBuildContext& ctx);
    void onMessagePublished(uint16_t messageId);
    void onMessageDropped(uint16_t messageId);

private:
    static constexpr uint8_t MaxProviders = 8;
    static constexpr uint32_t NumericThrottleMs = 10000U;

    struct Route {
        const IRuntimeSnapshotProvider* provider = nullptr;
        uint8_t snapshotIdx = 0;
        RuntimeRouteClass routeClass = RuntimeRouteClass::NumericThrottled;
        char suffix[Limits::TopicBuf] = {0};
        uint32_t lastPublishedTs = 0;
        uint32_t lastPublishMs = 0;
        uint32_t lastBuiltTs = 0;
        bool pending = true;
        bool force = true;
    };

    const MqttService* mqttSvc_ = nullptr;

    const IRuntimeSnapshotProvider* providers_[MaxProviders] = {};
    uint8_t providerCount_ = 0;

    Route routes_[Limits::MaxRuntimeRoutes] = {};
    uint8_t routeCount_ = 0;

    void markRoutePending_(uint8_t idx, bool force);
    void enqueueRoute_(uint8_t idx);
};

